//! This module provides a "server" which accepts and manages connections on a
//! unix-domain socket, using serialized nvlists to encode requests and
//! responses.  Request handlers are registered with `register_handler()` (for
//! operations that are processed concurrently, but don't concurrently modify
//! the connection's shared state), or `register_serial_handler()` (for
//! operations that "block the world" while they are being processed, and can
//! modify the connection-specific state).  See the method-level documentation
//! for more details.

use anyhow::anyhow;
use anyhow::Result;
use futures::{future, Future, FutureExt};
use log::*;
use nvpair::{NvEncoding, NvList};
use std::collections::HashMap;
use std::pin::Pin;
use std::sync::Arc;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::unix::{OwnedReadHalf, OwnedWriteHalf};
use tokio::net::{UnixListener, UnixStream};
use tokio::sync::{mpsc, Mutex};
use util::From64;

// Ss: ServerState (consumer's state associated with the server)
// Cs: ConnectionState (consumer's state associated with the connection)
pub struct Server<Ss, Cs> {
    socket_path: String,
    state: Ss,
    connection_handler: Box<ConnectionHandler<Ss, Cs>>,
    handlers: HashMap<String, HandlerEnum<Cs>>,
}

enum HandlerEnum<Cs> {
    Serial(Box<SerialHandler<Cs>>),
    Concurrent(Box<Handler<Cs>>),
}

type ConnectionHandler<Ss, Cs> = dyn Fn(&Ss) -> Cs + Send + Sync;

pub type HandlerReturn = Result<Pin<Box<dyn Future<Output = Result<Option<NvList>>> + Send>>>;
type Handler<Cs> = dyn Fn(&mut Cs, NvList) -> HandlerReturn + Send + Sync;

// 'a indicates that the returned Future can capture the `&mut Cs` reference
pub type SerialHandlerReturn<'a> =
    Pin<Box<dyn Future<Output = Result<Option<NvList>>> + Send + 'a>>;
type SerialHandler<Cs> = dyn Fn(&mut Cs, NvList) -> SerialHandlerReturn + Send + Sync;

impl<Ss: Send + Sync + 'static, Cs: Send + Sync + 'static> Server<Ss, Cs> {
    /// The connection_handler will be called when a new connection is
    /// established.  It is passed the server_state (Ss) and returns a
    /// connection_state (Cs), which is passed to each of the Handlers.
    pub fn new(
        socket_path: &str,
        server_state: Ss,
        connection_handler: Box<ConnectionHandler<Ss, Cs>>,
    ) -> Server<Ss, Cs> {
        Server {
            socket_path: socket_path.to_owned(),
            state: server_state,
            connection_handler,
            handlers: Default::default(),
        }
    }

    /// Register a function to be called for a regular, concurrent operation.
    /// When a connection receives a request with "Type" = request_type, the
    /// Handler will be called.  The Handler returns a Future, which the server
    /// will run in a new task.  If either the Handler or its returned Future
    /// return an Err, the connection will be closed.  This should primarily be
    /// used when the request is invalid.
    ///
    /// Note that since the Handler takes `&mut Cs` (a mutable reference to the
    /// connection state), the Handler can mutate the state, but the Future
    /// which it returns can not (it's run in the background while we are
    /// handling other requests).  If you need to manipulate the connection
    /// state from async code, consider using register_serial_handler() instead.
    pub fn register_handler(&mut self, request_type: &str, handler: Box<Handler<Cs>>) {
        self.handlers
            .insert(request_type.to_owned(), HandlerEnum::Concurrent(handler));
    }

    /// Register a function to be called for a "serial" operation.  The server
    /// awaits for the returned future to complete before processing the next
    /// operation.  This should only be used for operations that don't need to
    /// be processed concurrently with other operations.  Note that unlike a
    /// regular Handler, the SerialHandler's returned Future can capture the
    /// `&mut Cs` (which is indicated by the SerialHandlerReturn type).  This is
    /// especially useful if you need to manipulate the connection state from
    /// async code.
    pub fn register_serial_handler(&mut self, request_type: &str, handler: Box<SerialHandler<Cs>>) {
        self.handlers
            .insert(request_type.to_owned(), HandlerEnum::Serial(handler));
    }

    /// Start the server by creating a new unix-domain socket and spawning a new
    /// task which will accept connections on it.
    pub fn start(self) {
        let arc = Arc::new(self);
        info!("Listening on: {}", arc.socket_path);
        let _ = std::fs::remove_file(&arc.socket_path);
        let listener = UnixListener::bind(&arc.socket_path).unwrap();
        tokio::spawn(async move {
            loop {
                match listener.accept().await {
                    Ok((stream, _)) => {
                        info!("accepted connection on {}", arc.socket_path);
                        let connection_state = (arc.connection_handler)(&arc.state);
                        let server = arc.clone();
                        tokio::spawn(async move {
                            if let Err(e) =
                                Self::start_connection(server, stream, connection_state).await
                            {
                                error!("closing connection due to error: {:?}", e);
                            }
                        });
                    }
                    Err(e) => {
                        warn!("accept() on {} failed: {}", arc.socket_path, e);
                    }
                }
            }
        });
    }

    async fn get_next_request(input: &mut OwnedReadHalf) -> tokio::io::Result<NvList> {
        // XXX kernel sends this as host byte order
        let len64 = input.read_u64_le().await?;
        //trace!("got request len: {}", len64);
        if len64 > 20_000_000 {
            // max zfs block size is 16MB
            panic!("got unreasonable request length {} ({:#x})", len64, len64);
        }

        let mut v = Vec::new();
        // XXX would be nice if we didn't have to zero it out.  Should be able
        // to do that using read_buf(), treating the Vec as a BufMut, but will
        // require multiple calls to do the equivalent of read_exact().
        v.resize(usize::from64(len64), 0);
        input.read_exact(v.as_mut()).await?;
        let nvl = NvList::try_unpack(v.as_ref()).unwrap();
        Ok(nvl)
    }

    async fn send_response(output: &Mutex<OwnedWriteHalf>, nvl: NvList) {
        let buf = nvl.pack(NvEncoding::Native).unwrap();
        drop(nvl);
        let len64 = buf.len() as u64;
        let mut w = output.lock().await;
        // XXX kernel expects this as host byte order
        trace!("sending response of {} bytes", len64);
        w.write_u64_le(len64).await.unwrap();
        w.write_all(buf.as_slice()).await.unwrap();
    }

    async fn start_connection(
        server: Arc<Server<Ss, Cs>>,
        stream: UnixStream,
        mut state: Cs,
    ) -> Result<()> {
        let (mut input, output_raw) = stream.into_split();
        let output = Arc::new(Mutex::new(output_raw));

        let (error_tx, mut error_rx) = mpsc::channel(1);
        loop {
            if let Some(e) = error_rx.recv().now_or_never() {
                // an async (spawned) task produced an error
                return Err(e.unwrap());
            }
            let nvl = Self::get_next_request(&mut input).await?;

            let request_type_cstr = nvl.lookup_string("Type")?;
            let request_type = request_type_cstr.to_str()?;
            match server.handlers.get(request_type) {
                Some(HandlerEnum::Serial(handler)) => {
                    let response_opt = handler(&mut state, nvl).await?;
                    if let Some(response) = response_opt {
                        Self::send_response(&output, response).await;
                    }
                }
                Some(HandlerEnum::Concurrent(handler)) => {
                    let fut = handler(&mut state, nvl)?;
                    let output = output.clone();
                    let my_error_tx = error_tx.clone();
                    tokio::spawn(async move {
                        match fut.await {
                            Ok(Some(response)) => {
                                Self::send_response(&output, response).await;
                            }
                            Ok(None) => {}
                            Err(e) => {
                                my_error_tx.send(e).await.unwrap();
                            }
                        }
                    });
                }
                None => {
                    return Err(anyhow!("bad type {:?} in request {:?}", request_type, nvl));
                }
            }
        }
    }
}

/// Helper function to produce the value that should be returned from a handler
/// that does not need to do any async work.
pub fn handler_return_ok(response: Option<NvList>) -> HandlerReturn {
    Ok(Box::pin(future::ready(Ok(response))))
}

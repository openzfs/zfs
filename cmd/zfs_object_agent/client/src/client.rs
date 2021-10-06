use nvpair::NvEncoding;
use nvpair::NvList;
use nvpair::NvListRef;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::unix::{OwnedReadHalf, OwnedWriteHalf};
use tokio::task::JoinHandle;
use zettacache::base_types::*;
use zettaobject::base_types::*;

pub struct Client {
    input: Option<OwnedReadHalf>,
    output: OwnedWriteHalf,
}

impl Client {
    pub async fn connect() -> Client {
        let s = tokio::net::UnixStream::connect("/etc/zfs/zfs_root_socket")
            .await
            .unwrap();

        let (r, w) = s.into_split();
        Client {
            input: Some(r), // None while get_responses_initiate() is running
            output: w,
        }
    }

    async fn get_next_response_impl(input: &mut OwnedReadHalf) -> NvList {
        let len64 = input.read_u64_le().await.unwrap();
        let mut v = Vec::new();
        v.resize(len64 as usize, 0);
        input.read_exact(v.as_mut()).await.unwrap();
        let nvl = NvList::try_unpack(v.as_ref()).unwrap();
        println!("got response: {:?}", nvl);
        nvl
    }

    pub async fn get_next_response(&mut self) -> NvList {
        Self::get_next_response_impl(self.input.as_mut().unwrap()).await
    }

    /// Only one of these can be running at a time; call get_responses_join() on
    /// the returned value to wait.
    pub fn get_responses_initiate(&mut self, num: usize) -> JoinHandle<OwnedReadHalf> {
        let mut input = self.input.take().unwrap();
        tokio::spawn(async move {
            for _ in 0..num {
                Self::get_next_response_impl(&mut input).await;
            }
            input
        })
    }

    // If we wanted to get fancy, this could return a Vec<NvList> of the responses
    pub async fn get_responses_join(&mut self, handle: JoinHandle<OwnedReadHalf>) {
        self.input = Some(handle.await.unwrap());
    }

    async fn send_request(&mut self, nvl: &NvListRef) {
        println!("sending request: {:?}", nvl);
        let buf = nvl.pack(NvEncoding::Native).unwrap();
        self.output.write_u64_le(buf.len() as u64).await.unwrap();
        self.output.write_all(buf.as_ref()).await.unwrap();
    }

    pub async fn create_pool(
        &mut self,
        region: &str,
        endpoint: &str,
        bucket_name: &str,
        guid: PoolGuid,
        name: &str,
    ) {
        let mut nvl = NvList::new_unique_names();

        nvl.insert("Type", "create pool").unwrap();
        nvl.insert("region", region).unwrap();
        nvl.insert("endpoint", endpoint).unwrap();
        nvl.insert("bucket", bucket_name).unwrap();
        nvl.insert("GUID", &guid.0).unwrap();
        nvl.insert("name", name).unwrap();

        self.send_request(nvl.as_ref()).await;
    }

    pub async fn open_pool(
        &mut self,
        region: &str,
        endpoint: &str,
        bucket_name: &str,
        guid: PoolGuid,
    ) {
        let mut nvl = NvList::new_unique_names();

        nvl.insert("Type", "open pool").unwrap();
        nvl.insert("region", region).unwrap();
        nvl.insert("endpoint", endpoint).unwrap();
        nvl.insert("bucket", bucket_name).unwrap();
        nvl.insert("GUID", &guid.0).unwrap();
        self.send_request(nvl.as_ref()).await;
    }

    pub async fn read_block(&mut self, guid: PoolGuid, block: BlockId) {
        let mut nvl = NvList::new_unique_names();
        nvl.insert("Type", "read block").unwrap();
        nvl.insert("GUID", &guid.0).unwrap();
        nvl.insert("block", &block.0).unwrap();
        nvl.insert("request_id", &1234u64).unwrap();
        self.send_request(nvl.as_ref()).await;
    }

    pub async fn write_block(&mut self, guid: PoolGuid, block: BlockId, data: &[u8]) {
        let mut nvl = NvList::new_unique_names();
        nvl.insert("Type", "write block").unwrap();
        nvl.insert("GUID", &guid.0).unwrap();
        nvl.insert("block", &block.0).unwrap();
        nvl.insert("data", data).unwrap();
        self.send_request(nvl.as_ref()).await;
    }

    pub async fn free_block(&mut self, guid: PoolGuid, block: BlockId) {
        let mut nvl = NvList::new_unique_names();
        nvl.insert("Type", "free block").unwrap();
        nvl.insert("GUID", &guid.0).unwrap();
        nvl.insert("block", &block.0).unwrap();
        self.send_request(nvl.as_ref()).await;
    }

    pub async fn begin_txg(&mut self, guid: PoolGuid, txg: Txg) {
        let mut nvl = NvList::new_unique_names();
        nvl.insert("Type", "begin txg").unwrap();
        nvl.insert("GUID", &guid.0).unwrap();
        nvl.insert("TXG", &txg.0).unwrap();
        self.send_request(nvl.as_ref()).await;
    }

    pub async fn end_txg(&mut self, guid: PoolGuid, uberblock: &[u8]) {
        let mut nvl = NvList::new_unique_names();
        nvl.insert("Type", "end txg").unwrap();
        nvl.insert("GUID", &guid.0).unwrap();
        nvl.insert("data", uberblock).unwrap();
        self.send_request(nvl.as_ref()).await;
    }

    pub async fn flush_writes(&mut self, guid: PoolGuid) {
        let mut nvl = NvList::new_unique_names();
        nvl.insert("Type", "flush writes").unwrap();
        nvl.insert("GUID", &guid.0).unwrap();
        self.send_request(nvl.as_ref()).await;
    }
}

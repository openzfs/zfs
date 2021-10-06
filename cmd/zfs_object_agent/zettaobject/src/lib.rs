#![warn(clippy::cast_lossless)]
#![warn(clippy::cast_possible_truncation)]
#![warn(clippy::cast_possible_wrap)]
#![warn(clippy::cast_sign_loss)]

pub mod base_types;
mod data_object;
mod features;
mod heartbeat;
pub mod init;
mod object_access;
mod object_based_log;
mod object_block_map;
mod pool;
mod pool_destroy;
mod public_connection;
mod root_connection;
mod server;

pub use object_access::ObjectAccess;
pub use pool::Pool;

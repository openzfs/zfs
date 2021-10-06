use config::{Config, ConfigError};
use lazy_static::lazy_static;
use log::*;
use serde::Deserialize;
use std::{fmt::Debug, sync::RwLock};

lazy_static! {
    pub static ref CONFIG: RwLock<Config> = Default::default();
}

pub fn get_tunable<'de, T>(name: &str, default: T) -> T
where
    T: Deserialize<'de> + Debug,
{
    match CONFIG.read().unwrap().get(name) {
        Ok(v) => {
            info!("{}: using value {:?} from config file", name, v);
            v
        }
        Err(ConfigError::NotFound(_)) => default,
        Err(e) => {
            warn!("{}: using default: {:?}", e, default);
            default
        }
    }
}

pub fn read_tunable_config(file_name: &str) {
    let mut config = CONFIG.write().unwrap();
    config.merge(config::File::with_name(file_name)).unwrap();
    info!("config: {}", config.cache);
}

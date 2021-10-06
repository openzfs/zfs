use lazy_static::lazy_static;
use serde::{Deserialize, Serialize};
use std::{borrow::Borrow, collections::HashSet, fmt::Display, hash::Hash};

use crate::pool::PoolOpenError;

#[derive(Debug, Serialize, Deserialize, PartialEq, Eq, Clone)]
pub enum RequiredLevel {
    Optional,
    RequiredForRead,
    RequiredForWrite,
}

#[derive(Debug, Serialize, Deserialize, Eq, Clone)]
pub struct FeatureFlag {
    pub name: String,
    pub required: RequiredLevel,
}

impl Hash for FeatureFlag {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.name.hash(state);
    }
}

impl PartialEq for FeatureFlag {
    fn eq(&self, other: &Self) -> bool {
        self.name == other.name
    }
}

impl Borrow<str> for FeatureFlag {
    fn borrow(&self) -> &str {
        &self.name
    }
}

impl Display for FeatureFlag {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_fmt(format_args!("{} {:?}", self.name, self.required))
    }
}

/*
* Example of how to add a new featureflag:
*
    pub static ref FEATURE_NAME: FeatureFlag = FeatureFlag {
        name: "com.delphix:featurename".to_string(),
        required: RequiredLevel::RequiredForWrite
    };
*
* Note that corresponding entries need to be added to the zfeature code in
* module/zcommon/zfeature_common.c to expose them to userland and the kernel. Dependencies are
* stored and checked in that code, so need not be listed here.
*/

lazy_static! {
    static ref SUPPORTED_FEATURES: HashSet<FeatureFlag> = [/*FEATURE_NAME*/].iter().cloned().collect();
}

pub fn get_feature(name: &str) -> Option<FeatureFlag> {
    SUPPORTED_FEATURES.get(name).map(FeatureFlag::clone)
}

#[derive(Debug)]
pub struct FeatureError {
    pub features: Vec<FeatureFlag>,
    pub readonly: bool,
}

impl Display for FeatureError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_fmt(format_args!(
            "Missing features: {:?}{}",
            self.features,
            if self.readonly {
                " (readonly compatible)"
            } else {
                ""
            }
        ))
    }
}

impl std::error::Error for FeatureError {}

impl From<FeatureError> for PoolOpenError {
    fn from(e: FeatureError) -> Self {
        PoolOpenError::Feature(e)
    }
}

pub fn check_features<'a, I>(feature_list: I, readonly: bool) -> Result<(), FeatureError>
where
    I: Iterator<Item = &'a FeatureFlag>,
{
    let mut incompatible_features = vec![];
    let mut readonly_pass = true;
    for feature in feature_list {
        if SUPPORTED_FEATURES.get(feature).is_none() {
            match feature.required {
                RequiredLevel::Optional => {}
                RequiredLevel::RequiredForRead => {
                    incompatible_features.push(feature.clone());
                    readonly_pass = false;
                }
                RequiredLevel::RequiredForWrite => {
                    if !readonly {
                        incompatible_features.push(feature.clone());
                    }
                }
            }
        }
    }
    if !incompatible_features.is_empty() {
        Err(FeatureError {
            features: incompatible_features,
            readonly: readonly_pass,
        })
    } else {
        Ok(())
    }
}

pub use bindings::zfs_btree_add as add;
pub use bindings::zfs_btree_clear as clear;
pub use bindings::zfs_btree_create as create;
pub use bindings::zfs_btree_destroy as destroy;

pub unsafe fn iter<Node>(bt: *mut bindings::zfs_btree_t) -> impl Iterator<Item = *mut Node> {
    let mut w = std::mem::MaybeUninit::zeroed().assume_init();
    let mut e: *mut Node = bindings::zfs_btree_first(bt, &mut w) as *mut _;
    std::iter::from_fn(move || {
        if e == std::ptr::null_mut() {
            None
        } else {
            let ret = Some(e);
            e = bindings::zfs_btree_next(bt, &mut w, &mut w) as *mut _;
            ret
        }
    })
}

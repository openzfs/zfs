#[repr(C)]
pub struct SendPtr<T>(pub *mut T);

impl<T> From<*mut T> for SendPtr<T> {
    fn from(p: *mut T) -> Self {
        SendPtr(p)
    }
}

impl<T> From<&mut T> for SendPtr<T> {
    fn from(p: &mut T) -> Self {
        SendPtr(p as *mut T)
    }
}

impl<T> Clone for SendPtr<T> {
    fn clone(&self) -> SendPtr<T> {
        SendPtr(self.0)
    }
}
impl<T> Copy for SendPtr<T> {}

unsafe impl<T> Send for SendPtr<T> {}
unsafe impl<T> Sync for SendPtr<T> {}

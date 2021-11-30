use super::util::*;
use crate::libzpool;

#[test]
fn zilpmem_prb_gc() {
    libzpool::init_once();

    let cd1 = HeapChunkData::new_zeroed(512);
    let cd2 = HeapChunkData::new_zeroed(512);
    let c1 = alloc_heap_chunk(&cd1);
    let c2 = alloc_heap_chunk(&cd2);

    let mut prb = Prb::new_empty(1);
    prb.add_chunk_for_write(&c1);
    prb.add_chunk_for_write(&c2);

    let mut zh = ZilHeaderPmem::init();

    let os = prb.setup_objset(0x1, Some(&mut zh));

    let body = vec![23, 42];

    let e1 = os.write_entry_mustnotblock(1, true, &body);
    let e2 = os.write_entry_mustnotblock(1, true, &body);

    assert!(c1.contains(e1.entry_pmem_base));
    assert!(!c2.contains(e1.entry_pmem_base), "impl broken");
    assert!(c2.contains(e2.entry_pmem_base));
    assert!(!c1.contains(e2.entry_pmem_base), "impl broken");
    assert_eq!(c1, e1.entry_chunk_ptr());
    assert_eq!(c2, e2.entry_chunk_ptr());

    use bindings::entry_header_data_t;
    let e: Vec<_> = cd1
        .iter()
        .zip(std::iter::repeat(c1))
        .chain(cd2.iter().zip(std::iter::repeat(c2)))
        .map(|(e, c)| unsafe {
            let e = e.unwrap();
            let hdr: &entry_header_data_t = &*(e as *mut entry_header_data_t);
            (SynthEntry::from(hdr), c)
        })
        .collect();
    println!("{:#?}", e);

    // assert that we are full by now
    let (_, res) = os.write_entry(1, true, &body, false);
    assert_eq!(res, bindings::prb_write_result_t_PRB_WRITE_EWOULDSLEEP);

    // do a gc run
    prb.gc();

    // TODO assert writing lower txg causes error

    // fill it again
    os.write_entry_mustnotblock(2, true, &body);
    os.write_entry_mustnotblock(2, true, &body);

    assert!(false);
}

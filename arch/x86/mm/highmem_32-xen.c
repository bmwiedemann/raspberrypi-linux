#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/swap.h> /* for totalram_pages */

void *kmap(struct page *page)
{
	might_sleep();
	if (!PageHighMem(page))
		return page_address(page);
	return kmap_high(page);
}
EXPORT_SYMBOL(kmap);

void kunmap(struct page *page)
{
	if (in_interrupt())
		BUG();
	if (!PageHighMem(page))
		return;
	kunmap_high(page);
}
EXPORT_SYMBOL(kunmap);

/*
 * kmap_atomic/kunmap_atomic is significantly faster than kmap/kunmap because
 * no global lock is needed and because the kmap code must perform a global TLB
 * invalidation when the kmap pool wraps.
 *
 * However when holding an atomic kmap it is not legal to sleep, so atomic
 * kmaps are appropriate for short, tight code paths only.
 */
void *kmap_atomic_prot(struct page *page, pgprot_t prot)
{
	unsigned long vaddr;
	int idx, type;

	/* even !CONFIG_PREEMPT needs this, for in_atomic in do_page_fault */
	pagefault_disable();

	if (!PageHighMem(page))
		return page_address(page);

	type = kmap_atomic_idx_push();
	idx = type + KM_TYPE_NR*smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	BUG_ON(!pte_none(*(kmap_pte-idx)));
	set_pte_at(&init_mm, vaddr, kmap_pte-idx, mk_pte(page, prot));

	return (void *)vaddr;
}
EXPORT_SYMBOL(kmap_atomic_prot);

void *__kmap_atomic(struct page *page)
{
	return kmap_atomic_prot(page, kmap_prot);
}
EXPORT_SYMBOL(__kmap_atomic);

/*
 * This is the same as kmap_atomic() but can map memory that doesn't
 * have a struct page associated with it.
 */
void *kmap_atomic_pfn(unsigned long pfn)
{
	return kmap_atomic_prot_pfn(pfn, kmap_prot);
}
EXPORT_SYMBOL_GPL(kmap_atomic_pfn);

void __kunmap_atomic(void *kvaddr)
{
	unsigned long vaddr = (unsigned long) kvaddr & PAGE_MASK;

	if (vaddr >= __fix_to_virt(FIX_KMAP_END) &&
	    vaddr <= __fix_to_virt(FIX_KMAP_BEGIN)) {
		int idx, type;

		type = kmap_atomic_idx();
		idx = type + KM_TYPE_NR * smp_processor_id();

#ifdef CONFIG_DEBUG_HIGHMEM
		WARN_ON_ONCE(vaddr != __fix_to_virt(FIX_KMAP_BEGIN + idx));
#endif
		/*
		 * Force other mappings to Oops if they'll try to access this
		 * pte without first remap it.  Keeping stale mappings around
		 * is a bad idea also, in case the page changes cacheability
		 * attributes or becomes a protected page in a hypervisor.
		 */
		kpte_clear_flush(kmap_pte-idx, vaddr);
		kmap_atomic_idx_pop();
	}
#ifdef CONFIG_DEBUG_HIGHMEM
	else {
		BUG_ON(vaddr < PAGE_OFFSET);
		BUG_ON(vaddr >= (unsigned long)high_memory);
	}
#endif

	pagefault_enable();
}
EXPORT_SYMBOL(__kunmap_atomic);

struct page *kmap_atomic_to_page(void *ptr)
{
	unsigned long idx, vaddr = (unsigned long)ptr;
	pte_t *pte;

	if (vaddr < FIXADDR_START)
		return virt_to_page(ptr);

	idx = virt_to_fix(vaddr);
	pte = kmap_pte - (idx - FIX_KMAP_BEGIN);
	return pte_page(*pte);
}
EXPORT_SYMBOL(kmap_atomic_to_page);

void clear_highpage(struct page *page)
{
	void *kaddr;

	if (likely(xen_feature(XENFEAT_highmem_assist))
	    && PageHighMem(page)) {
		struct mmuext_op meo;

		meo.cmd = MMUEXT_CLEAR_PAGE;
		meo.arg1.mfn = pfn_to_mfn(page_to_pfn(page));
		if (HYPERVISOR_mmuext_op(&meo, 1, NULL, DOMID_SELF) == 0)
			return;
	}

	kaddr = kmap_atomic(page, KM_USER0);
	clear_page(kaddr);
	kunmap_atomic(kaddr, KM_USER0);
}
EXPORT_SYMBOL(clear_highpage);

void copy_highpage(struct page *to, struct page *from)
{
	void *vfrom, *vto;

	if (likely(xen_feature(XENFEAT_highmem_assist))
	    && (PageHighMem(from) || PageHighMem(to))) {
		unsigned long from_pfn = page_to_pfn(from);
		unsigned long to_pfn = page_to_pfn(to);
		struct mmuext_op meo;

		meo.cmd = MMUEXT_COPY_PAGE;
		meo.arg1.mfn = pfn_to_mfn(to_pfn);
		meo.arg2.src_mfn = pfn_to_mfn(from_pfn);
		if (mfn_to_pfn(meo.arg2.src_mfn) == from_pfn
		    && mfn_to_pfn(meo.arg1.mfn) == to_pfn
		    && HYPERVISOR_mmuext_op(&meo, 1, NULL, DOMID_SELF) == 0)
			return;
	}

	vfrom = kmap_atomic(from, KM_USER0);
	vto = kmap_atomic(to, KM_USER1);
	copy_page(vto, vfrom);
	kunmap_atomic(vfrom, KM_USER0);
	kunmap_atomic(vto, KM_USER1);
}
EXPORT_SYMBOL(copy_highpage);

void __init set_highmem_pages_init(void)
{
	struct zone *zone;
	int nid;

	for_each_zone(zone) {
		unsigned long zone_start_pfn, zone_end_pfn;

		if (!is_highmem(zone))
			continue;

		zone_start_pfn = zone->zone_start_pfn;
		zone_end_pfn = zone_start_pfn + zone->spanned_pages;

		nid = zone_to_nid(zone);
		printk(KERN_INFO "Initializing %s for node %d (%08lx:%08lx)\n",
				zone->name, nid, zone_start_pfn, zone_end_pfn);

		add_highpages_with_active_regions(nid, zone_start_pfn,
				 zone_end_pfn);

		/* XEN: init high-mem pages outside initial allocation. */
		if (zone_start_pfn < xen_start_info->nr_pages)
			zone_start_pfn = xen_start_info->nr_pages;
		for (; zone_start_pfn < zone_end_pfn; zone_start_pfn++) {
			ClearPageReserved(pfn_to_page(zone_start_pfn));
			init_page_count(pfn_to_page(zone_start_pfn));
		}
	}
	totalram_pages += totalhigh_pages;
}
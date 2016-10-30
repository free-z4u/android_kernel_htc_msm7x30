/*
 * drivers/staging/android/ion/ion_system_heap.c
 *
 * Copyright (C) 2011 Google, Inc.
 * Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <asm/page.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/ion.h>
#include "ion_priv.h"
#include <linux/iommu.h>
#include <linux/seq_file.h>
#include <mach/iommu_domains.h>
#include <mach/msm_mem.h>
#include <asm/cacheflush.h>
#include <linux/msm_ion.h>
#include <linux/dma-mapping.h>

static atomic_t system_heap_allocated;
static atomic_t system_contig_heap_allocated;
static unsigned int system_heap_has_outer_cache;
static unsigned int system_heap_contig_has_outer_cache;
#if 0
static gfp_t high_order_gfp_flags = (GFP_HIGHUSER | __GFP_ZERO | __GFP_NOWARN |
				     __GFP_NORETRY) & ~__GFP_WAIT;
static gfp_t low_order_gfp_flags  = (GFP_HIGHUSER | __GFP_ZERO | __GFP_NOWARN);
static const unsigned int orders[] = {8, 4, 0};
static const int num_orders = ARRAY_SIZE(orders);
static int order_to_index(unsigned int order)
{
	int i;

	for (i = 0; i < num_orders; i++)
		if (order == orders[i])
			return i;
	BUG();
	return -1;
}

static inline unsigned int order_to_size(int order)
{
	return PAGE_SIZE << order;
}

struct ion_system_heap {
	struct ion_heap heap;
	struct ion_page_pool *pools[0];
};

static struct page *alloc_buffer_page(struct ion_system_heap *heap,
				      struct ion_buffer *buffer,
				      unsigned long order)
{
	bool cached = ion_buffer_cached(buffer);
	struct ion_page_pool *pool = heap->pools[order_to_index(order)];
	struct page *page;

	if (!cached) {
		page = ion_page_pool_alloc(pool);
	} else {
		gfp_t gfp_flags = low_order_gfp_flags;

		if (order > 4)
			gfp_flags = high_order_gfp_flags;
		page = alloc_pages(gfp_flags | __GFP_COMP, order);
		if (!page)
			return NULL;
		ion_pages_sync_for_device(NULL, page, PAGE_SIZE << order,
						DMA_BIDIRECTIONAL);
	}

	return page;
}

static void free_buffer_page(struct ion_system_heap *heap,
			     struct ion_buffer *buffer, struct page *page)
{
	unsigned int order = compound_order(page);
	bool cached = ion_buffer_cached(buffer);

	if (!cached && !(buffer->private_flags & ION_PRIV_FLAG_SHRINKER_FREE)) {
		struct ion_page_pool *pool = heap->pools[order_to_index(order)];

		ion_page_pool_free(pool, page);
	} else {
		__free_pages(page, order);
	}
}


static struct page *alloc_largest_available(struct ion_system_heap *heap,
					    struct ion_buffer *buffer,
					    unsigned long size,
					    unsigned int max_order)
{
	struct page *page;
	int i;

	for (i = 0; i < num_orders; i++) {
		if (size < order_to_size(orders[i]))
			continue;
		if (max_order < orders[i])
			continue;

		page = alloc_buffer_page(heap, buffer, orders[i]);
		if (!page)
			continue;

		return page;
	}

	return NULL;
}
#endif

static int ion_system_heap_allocate(struct ion_heap *heap,
				     struct ion_buffer *buffer,
				     unsigned long size, unsigned long align,
				     unsigned long flags)
{
	struct sg_table *table;
	struct scatterlist *sg;
	int i, j;
	int npages = PAGE_ALIGN(size) / PAGE_SIZE;

	table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;
	i = sg_alloc_table(table, npages, GFP_KERNEL);
	if (i)
		goto free_pages;
	for_each_sg(table->sgl, sg, table->nents, i) {
		struct page *page;
		page = alloc_page(GFP_KERNEL|__GFP_ZERO);
		if (!page)
			goto free_table;
		sg_set_page(sg, page, PAGE_SIZE, 0);
	}
	buffer->priv_virt = table;
	atomic_add(size, &system_heap_allocated);
	return 0;
free_table:
	for_each_sg(table->sgl, sg, i, j)
		__free_page(sg_page(sg));
	sg_free_table(table);
free_pages:
	kfree(table);
	return -ENOMEM;
}

static void ion_system_heap_free(struct ion_buffer *buffer)
{
	int i;
	struct scatterlist *sg;
	struct sg_table *table = buffer->priv_virt;

	for_each_sg(table->sgl, sg, table->nents, i)
		__free_page(sg_page(sg));
	if (buffer->sg_table)
		sg_free_table(buffer->sg_table);
	kfree(buffer->sg_table);
	atomic_sub(buffer->size, &system_heap_allocated);
}

static struct sg_table *ion_system_heap_map_dma(struct ion_heap *heap,
						struct ion_buffer *buffer)
{
	return buffer->priv_virt;
}

static void ion_system_heap_unmap_dma(struct ion_heap *heap,
				      struct ion_buffer *buffer)
{
	return;
}

void *ion_system_heap_map_kernel(struct ion_heap *heap,
				 struct ion_buffer *buffer)
{
	if (!ION_IS_CACHED(buffer->flags)) {
		pr_err("%s: cannot map system heap uncached\n", __func__);
		return ERR_PTR(-EINVAL);
	} else {
		struct scatterlist *sg;
		int i;
		void *vaddr;
		struct sg_table *table = buffer->priv_virt;
		struct page **pages = kmalloc(
					sizeof(struct page *) * table->nents,
					GFP_KERNEL);

		for_each_sg(table->sgl, sg, table->nents, i)
			pages[i] = sg_page(sg);
		vaddr = vmap(pages, table->nents, VM_MAP, PAGE_KERNEL);
		kfree(pages);

		return vaddr;
	}
}

void ion_system_heap_unmap_kernel(struct ion_heap *heap,
				  struct ion_buffer *buffer)
{
	vunmap(buffer->vaddr);
}

void ion_system_heap_unmap_iommu(struct ion_iommu_map *data)
{
	unsigned int domain_num;
	unsigned int partition_num;
	struct iommu_domain *domain;

	if (!msm_use_iommu())
		return;

	domain_num = iommu_map_domain(data);
	partition_num = iommu_map_partition(data);

	domain = msm_get_iommu_domain(domain_num);

	if (!domain) {
		WARN(1, "Could not get domain %d. Corruption?\n", domain_num);
		return;
	}

	iommu_unmap_range(domain, data->iova_addr, data->mapped_size);
	msm_free_iova_address(data->iova_addr, domain_num, partition_num,
				data->mapped_size);

	return;
}

#if 0
static int ion_system_heap_shrink(struct ion_heap *heap, gfp_t gfp_mask,
					int nr_to_scan)
{
	struct ion_system_heap *sys_heap;
	int nr_total = 0;
	int i;

	sys_heap = container_of(heap, struct ion_system_heap, heap);

	for (i = 0; i < num_orders; i++) {
		struct ion_page_pool *pool = sys_heap->pools[i];

		nr_total += ion_page_pool_shrink(pool, gfp_mask, nr_to_scan);
	}

	return nr_total;
}

static struct ion_heap_ops system_heap_ops = {
	.allocate = ion_system_heap_allocate,
	.free = ion_system_heap_free,
	.map_dma = ion_system_heap_map_dma,
	.unmap_dma = ion_system_heap_unmap_dma,
	.map_kernel = ion_heap_map_kernel,
	.unmap_kernel = ion_heap_unmap_kernel,
	.map_user = ion_heap_map_user,
	.shrink = ion_system_heap_shrink,
};
#endif

int ion_system_heap_map_user(struct ion_heap *heap, struct ion_buffer *buffer,
			     struct vm_area_struct *vma)
{
	if (!ION_IS_CACHED(buffer->flags)) {
		pr_err("%s: cannot map system heap uncached\n", __func__);
		return -EINVAL;
	} else {
		struct sg_table *table = buffer->priv_virt;
		unsigned long addr = vma->vm_start;
		unsigned long offset = vma->vm_pgoff;
		struct scatterlist *sg;
		int i;

		for_each_sg(table->sgl, sg, table->nents, i) {
			if (offset) {
				offset--;
				continue;
			}
			vm_insert_page(vma, addr, sg_page(sg));
			addr += PAGE_SIZE;
		}
		return 0;
	}
}

int ion_system_heap_cache_ops(struct ion_heap *heap, struct ion_buffer *buffer,
			void *vaddr, unsigned int offset, unsigned int length,
			unsigned int cmd)
{
	void (*outer_cache_op)(phys_addr_t, phys_addr_t);

	switch (cmd) {
	case ION_IOC_CLEAN_CACHES:
		if (!vaddr)
			dma_sync_sg_for_device(NULL, buffer->sg_table->sgl,
				buffer->sg_table->nents, DMA_TO_DEVICE);
		else
			dmac_clean_range(vaddr, vaddr + length);
		outer_cache_op = outer_clean_range;
		break;
	case ION_IOC_INV_CACHES:
		if (!vaddr)
			dma_sync_sg_for_cpu(NULL, buffer->sg_table->sgl,
				buffer->sg_table->nents, DMA_FROM_DEVICE);
		else
			dmac_inv_range(vaddr, vaddr + length);
		outer_cache_op = outer_inv_range;
		break;
	case ION_IOC_CLEAN_INV_CACHES:
		if (!vaddr) {
			dma_sync_sg_for_device(NULL, buffer->sg_table->sgl,
				buffer->sg_table->nents, DMA_TO_DEVICE);
			dma_sync_sg_for_cpu(NULL, buffer->sg_table->sgl,
				buffer->sg_table->nents, DMA_FROM_DEVICE);
		} else {
			dmac_flush_range(vaddr, vaddr + length);
		}
		outer_cache_op = outer_flush_range;
		break;
	default:
		return -EINVAL;
	}

	if (system_heap_has_outer_cache) {
		unsigned long pstart;
		struct sg_table *table = buffer->priv_virt;
		struct scatterlist *sg;
		int i;
		for_each_sg(table->sgl, sg, table->nents, i) {
			struct page *page = sg_page(sg);
			pstart = page_to_phys(page);
			/*
			 * If page -> phys is returning NULL, something
			 * has really gone wrong...
			 */
			if (!pstart) {
				WARN(1, "Could not translate virtual address to physical address\n");
				return -EINVAL;
			}
			outer_cache_op(pstart, pstart + PAGE_SIZE);
		}
	}
	return 0;
}

static int ion_system_print_debug(struct ion_heap *heap, struct seq_file *s,
				  const struct rb_root *unused)
{
	seq_printf(s, "total bytes currently allocated: %lx\n",
			(unsigned long) atomic_read(&system_heap_allocated));

	return 0;
}

int ion_system_heap_map_iommu(struct ion_buffer *buffer,
				struct ion_iommu_map *data,
				unsigned int domain_num,
				unsigned int partition_num,
				unsigned long align,
				unsigned long iova_length,
				unsigned long flags)
{
	int ret = 0;
	struct iommu_domain *domain;
	unsigned long extra;
	unsigned long extra_iova_addr;
	struct sg_table *table = buffer->priv_virt;
	int prot = IOMMU_WRITE | IOMMU_READ;
	prot |= ION_IS_CACHED(flags) ? IOMMU_CACHE : 0;

	if (!ION_IS_CACHED(flags))
		return -EINVAL;

	if (!msm_use_iommu())
		return -EINVAL;

	data->mapped_size = iova_length;
	extra = iova_length - buffer->size;

	ret = msm_allocate_iova_address(domain_num, partition_num,
						data->mapped_size, align,
						&data->iova_addr);

	if (ret)
		goto out;

	domain = msm_get_iommu_domain(domain_num);

	if (!domain) {
		ret = -ENOMEM;
		goto out1;
	}

	ret = iommu_map_range(domain, data->iova_addr, table->sgl,
			      buffer->size, prot);

	if (ret) {
		pr_err("%s: could not map %lx in domain %p\n",
			__func__, data->iova_addr, domain);
		goto out1;
	}

	extra_iova_addr = data->iova_addr + buffer->size;
	if (extra) {
		ret = msm_iommu_map_extra(domain, extra_iova_addr, extra, SZ_4K,
					  prot);
		if (ret)
			goto out2;
	}
	return ret;

out2:
	iommu_unmap_range(domain, data->iova_addr, buffer->size);
out1:
	msm_free_iova_address(data->iova_addr, domain_num, partition_num,
				data->mapped_size);
out:
	return ret;
}

static struct ion_heap_ops vmalloc_ops = {
	.allocate = ion_system_heap_allocate,
	.free = ion_system_heap_free,
	.map_dma = ion_system_heap_map_dma,
	.unmap_dma = ion_system_heap_unmap_dma,
	.map_kernel = ion_system_heap_map_kernel,
	.unmap_kernel = ion_system_heap_unmap_kernel,
	.map_user = ion_system_heap_map_user,
	.cache_op = ion_system_heap_cache_ops,
	.print_debug = ion_system_print_debug,
	.map_iommu = ion_system_heap_map_iommu,
	.unmap_iommu = ion_system_heap_unmap_iommu,
};

struct ion_heap *ion_system_heap_create(struct ion_platform_heap *pheap)
{
	struct ion_heap *heap;

	heap = kzalloc(sizeof(struct ion_heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);
	heap->ops = &vmalloc_ops;
	heap->type = ION_HEAP_TYPE_SYSTEM;
	system_heap_has_outer_cache = pheap->has_outer_cache;
	return heap;
}

void ion_system_heap_destroy(struct ion_heap *heap)
{
	kfree(heap);
}

static int ion_system_contig_heap_allocate(struct ion_heap *heap,
					   struct ion_buffer *buffer,
					   unsigned long len,
					   unsigned long align,
					   unsigned long flags)
{
	buffer->priv_virt = kzalloc(len, GFP_KERNEL);
	if (!buffer->priv_virt)
		return -ENOMEM;
	atomic_add(len, &system_contig_heap_allocated);
	return 0;
}

void ion_system_contig_heap_free(struct ion_buffer *buffer)
{
	kfree(buffer->priv_virt);
	atomic_sub(buffer->size, &system_contig_heap_allocated);
}

static int ion_system_contig_heap_phys(struct ion_heap *heap,
				       struct ion_buffer *buffer,
				       ion_phys_addr_t *addr, size_t *len)
{
	*addr = virt_to_phys(buffer->priv_virt);
	*len = buffer->size;
	return 0;
}

static struct sg_table *ion_system_contig_heap_map_dma(struct ion_heap *heap,
						struct ion_buffer *buffer)
{
	struct sg_table *table;
	int ret;

	table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!table)
		return ERR_PTR(-ENOMEM);
	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret) {
		kfree(table);
		return ERR_PTR(ret);
	}
	sg_set_page(table->sgl, virt_to_page(buffer->priv_virt), buffer->size,
		    0);
	return table;
}

int ion_system_contig_heap_map_user(struct ion_heap *heap,
				    struct ion_buffer *buffer,
				    struct vm_area_struct *vma)
{
	unsigned long pfn = __phys_to_pfn(virt_to_phys(buffer->priv_virt));

	if (ION_IS_CACHED(buffer->flags))
		return remap_pfn_range(vma, vma->vm_start, pfn + vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
	else {
		pr_err("%s: cannot map system heap uncached\n", __func__);
		return -EINVAL;
	}
}

int ion_system_contig_heap_cache_ops(struct ion_heap *heap,
			struct ion_buffer *buffer, void *vaddr,
			unsigned int offset, unsigned int length,
			unsigned int cmd)
{
	void (*outer_cache_op)(phys_addr_t, phys_addr_t);

	switch (cmd) {
	case ION_IOC_CLEAN_CACHES:
		dmac_clean_range(vaddr, vaddr + length);
		outer_cache_op = outer_clean_range;
		break;
	case ION_IOC_INV_CACHES:
		dmac_inv_range(vaddr, vaddr + length);
		outer_cache_op = outer_inv_range;
		break;
	case ION_IOC_CLEAN_INV_CACHES:
		dmac_flush_range(vaddr, vaddr + length);
		outer_cache_op = outer_flush_range;
		break;
	default:
		return -EINVAL;
	}

	if (system_heap_contig_has_outer_cache) {
		unsigned long pstart;

		pstart = virt_to_phys(buffer->priv_virt) + offset;
		if (!pstart) {
			WARN(1, "Could not do virt to phys translation on %p\n",
				buffer->priv_virt);
			return -EINVAL;
		}

		outer_cache_op(pstart, pstart + PAGE_SIZE);
	}

	return 0;
}

static int ion_system_contig_print_debug(struct ion_heap *heap,
					 struct seq_file *s,
					 const struct rb_root *unused)
{
	seq_printf(s, "total bytes currently allocated: %lx\n",
		(unsigned long) atomic_read(&system_contig_heap_allocated));

	return 0;
}

int ion_system_contig_heap_map_iommu(struct ion_buffer *buffer,
				struct ion_iommu_map *data,
				unsigned int domain_num,
				unsigned int partition_num,
				unsigned long align,
				unsigned long iova_length,
				unsigned long flags)
{
	int ret = 0;
	struct iommu_domain *domain;
	unsigned long extra;
	struct scatterlist *sglist = 0;
	struct page *page = 0;
	int prot = IOMMU_WRITE | IOMMU_READ;
	prot |= ION_IS_CACHED(flags) ? IOMMU_CACHE : 0;

	if (!ION_IS_CACHED(flags))
		return -EINVAL;

	if (!msm_use_iommu()) {
		data->iova_addr = virt_to_phys(buffer->vaddr);
		return 0;
	}

	data->mapped_size = iova_length;
	extra = iova_length - buffer->size;

	ret = msm_allocate_iova_address(domain_num, partition_num,
						data->mapped_size, align,
						&data->iova_addr);

	if (ret)
		goto out;

	domain = msm_get_iommu_domain(domain_num);

	if (!domain) {
		ret = -ENOMEM;
		goto out1;
	}
	page = virt_to_page(buffer->vaddr);

	sglist = kmalloc(sizeof(*sglist), GFP_KERNEL);
	if (!sglist)
		goto out1;

	sg_init_table(sglist, 1);
	sg_set_page(sglist, page, buffer->size, 0);

	ret = iommu_map_range(domain, data->iova_addr, sglist,
			      buffer->size, prot);
	if (ret) {
		pr_err("%s: could not map %lx in domain %p\n",
			__func__, data->iova_addr, domain);
		goto out1;
	}

	if (extra) {
		unsigned long extra_iova_addr = data->iova_addr + buffer->size;
		ret = msm_iommu_map_extra(domain, extra_iova_addr, extra, SZ_4K,
					  prot);
		if (ret)
			goto out2;
	}
	kfree(sglist);
	return ret;
out2:
	iommu_unmap_range(domain, data->iova_addr, buffer->size);

out1:
	kfree(sglist);
	msm_free_iova_address(data->iova_addr, domain_num, partition_num,
						data->mapped_size);
out:
	return ret;
}

static struct ion_heap_ops kmalloc_ops = {
	.allocate = ion_system_contig_heap_allocate,
	.free = ion_system_contig_heap_free,
	.phys = ion_system_contig_heap_phys,
	.map_dma = ion_system_contig_heap_map_dma,
	.unmap_dma = ion_system_heap_unmap_dma,
	.map_kernel = ion_system_heap_map_kernel,
	.unmap_kernel = ion_system_heap_unmap_kernel,
	.map_user = ion_system_contig_heap_map_user,
	.cache_op = ion_system_contig_heap_cache_ops,
	.print_debug = ion_system_contig_print_debug,
	.map_iommu = ion_system_contig_heap_map_iommu,
	.unmap_iommu = ion_system_heap_unmap_iommu,
};

struct ion_heap *ion_system_contig_heap_create(struct ion_platform_heap *pheap)
{
	struct ion_heap *heap;

	heap = kzalloc(sizeof(struct ion_heap), GFP_KERNEL);
	if (!heap)
		return ERR_PTR(-ENOMEM);
	heap->ops = &kmalloc_ops;
	heap->type = ION_HEAP_TYPE_SYSTEM_CONTIG;
	system_heap_contig_has_outer_cache = pheap->has_outer_cache;
	return heap;
}

void ion_system_contig_heap_destroy(struct ion_heap *heap)
{
	kfree(heap);
}

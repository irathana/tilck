/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck/common/basic_defs.h>
#include <tilck/common/printk.h>

#include <tilck/kernel/errno.h>
#include <tilck/kernel/paging.h>
#include <tilck/kernel/process.h>
#include <tilck/kernel/process_mm.h>
#include <tilck/kernel/system_mmap.h>
#include <tilck/kernel/fs/vfs_base.h>
#include <tilck/kernel/fs/fat32.h>

int fat_ramdisk_prepare_for_mmap(struct fat_fs_device_data *d, size_t rd_size)
{
   struct fat_hdr *hdr = d->hdr;

   if (system_mmap_check_for_extra_ramdisk_region(hdr)) {

      /*
       * Typical case: the extra 4k region after our ramdisk, survived the
       * overlap handling, meaning that the was at least 4k of usable (regular)
       * memory just after our ramdisk. This will help in the corner case below.
       */

      rd_size += PAGE_SIZE;
   }

   if (d->cluster_size < PAGE_SIZE || !IS_PAGE_ALIGNED(d->cluster_size)) {
      /* We cannot support our implementation of mmap in this case */
      return -1;
   }

   retain_pageframes_mapped_at(get_kernel_pdir(), hdr, rd_size);

   if (fat_is_first_data_sector_aligned(hdr, PAGE_SIZE))
      return 0; /* Typical case: nothing to do */

   /*
    * The code here below will almost never be triggered as it handles a very
    * ugly use case. Let me explain.
    *
    * In order to fat_mmap() work in the simple and direct way as implemented
    * in Tilck, the FAT clusters must be aligned at page boundary. That is true
    * when just the first data sector is aligned. In our build system, the
    * fathack build app is used to make that happen. Fathack substantially calls
    * fat_align_first_data_sector() which adds more reserved sectors to the
    * partition by shifting all the data by the necessary amount of bytes.
    *
    * Now, when our FAT ramdisk is build by our build system, because of fathack
    * we never have to worry about such alignment. That is true even if the
    * fat ramdisk is later modified by an external tool to add/remove files from
    * it: the number of reserved sectors won't changed. BUT, in the unlikely
    * case when the external tools reformats the whole partition, we'll loose
    * that alignment and it would be nice if Tilck itself could handle that case
    * too. That's what the following code does.
    *
    * Note: in order to the code below to work (corner case, like explained) we
    * need at least one of the following conditions to be true:
    *
    *    - boot the OS using one of Tilck's bootloaders OR
    *    - have 1 page avail at the end of the ramdisk mem region (very likely)
    *
    * In summary, the code below won't work only in the 1 in billion case where
    * all of the following statements are true:
    *
    *    - the fatpart was NOT generated by our build system (why doing that?)
    *    - the OS was NOT booted using Tilck's bootloaders (e.g. using GRUB)
    *    - according to the firmware, the next 4k after the ramdisk belong
    *      to a reserved memory region (extremely unlucky case)
    */

   const u32 used = fat_calculate_used_bytes(hdr);
   pdir_t *const pdir = get_kernel_pdir();
   char *const va_begin = (char *)hdr;
   char *const va_end = va_begin + rd_size;
   VERIFY(rd_size >= used);

   if (rd_size - used < PAGE_SIZE) {
      printk("WARNING: [fat ramdisk] cannot align first data sector\n");
      return -1;
   }

   for (char *va = va_begin; va < va_end; va += PAGE_SIZE)
      set_page_rw(pdir, va, true);

   fat_align_first_data_sector(hdr, PAGE_SIZE);

   for (char *va = va_begin; va < va_end; va += PAGE_SIZE)
      set_page_rw(pdir, va, false);

   printk("fat ramdisk: align of ramdisk was necessary\n");
   return 0;
}

int fat_mmap(struct user_mapping *um, pdir_t *pdir, int flags)
{
   struct fatfs_handle *fh = um->h;
   struct fat_fs_device_data *d = fh->fs->device_data;
   const size_t off_begin = um->off;
   const size_t off_end = off_begin + um->len;
   ulong vaddr = um->vaddr, off = 0;
   size_t mapped_cnt, tot_mapped_cnt = 0;
   u32 clu;

   if (!d->mmap_support)
      return -ENODEV; /* We do NOT support mmap for this "superblock" */

   if (fh->e->directory)
      return -EACCES;

   if (flags & VFS_MM_DONT_MMAP)
      return 0;

   clu = fat_get_first_cluster(fh->e);

   do {

      char *data;
      const ulong clu_end_off = off + d->cluster_size;

      // Are we past the end of the mapped region?
      if (off >= off_end)
         break;

      // Does this cluster belong to the mapped region?
      if (clu_end_off > off_begin) {

         // The cluster ends *after* the beginning of our region
         data = fat_get_pointer_to_cluster_data(d->hdr, clu);

         if (off < off_begin) {

            // Our region begins somewhere in the middle of this cluster.
            // This can happen only with cluster_size > PAGE_SIZE.
            off = off_begin;
            data += off_begin - off;
         }

         /*
          * Calculate the number of pages to mmap, considering that:
          *    - we cannot mmap in this iteration further than clu_end_off
          *    - we must not mmap further than off_end
          */
         size_t pg_count = (MIN(clu_end_off, off_end) - off) >> PAGE_SHIFT;

         mapped_cnt = map_pages(pdir,
                                (void *)vaddr,
                                KERNEL_VA_TO_PA(data),
                                pg_count,
                                PAGING_FL_US | PAGING_FL_SHARED);

         if (mapped_cnt != pg_count) {
            unmap_pages_permissive(pdir,
                                   (void *)um->vaddr,
                                   tot_mapped_cnt,
                                   false);
            return -ENOMEM;
         }

         vaddr += pg_count << PAGE_SHIFT;
         off += pg_count << PAGE_SHIFT;
         tot_mapped_cnt += mapped_cnt;

         // After each iteration, `off` must always be aligned at `cluster_size`
         ASSERT((off % d->cluster_size) == 0);

      } else {

         // We skipped the whole cluster
         off += d->cluster_size;
      }

      // Get the next cluster# from the File Allocation Table
      clu = fat_read_fat_entry(d->hdr, d->type, 0, clu);

      // We do not expect BAD CLUSTERS
      ASSERT(!fat_is_bad_cluster(d->type, clu));

   } while (!fat_is_end_of_clusterchain(d->type, clu));

   return 0;
}

int fat_munmap(struct user_mapping *um, void *vaddrp, size_t len)
{
   struct fatfs_handle *fh = um->h;
   struct fat_fs_device_data *d = fh->fs->device_data;

   if (!d->mmap_support)
      return -ENODEV; /* We do NOT support mmap for this "superblock" */

   return generic_fs_munmap(um, vaddrp, len);
}

#include <stdint.h>
#include <stddef.h>
#include <lib/libc.h>
#include <drivers/disk.h>
#include <lib/real.h>
#include <lib/blib.h>
#include <lib/part.h>
#include <lib/print.h>
#include <mm/pmm.h>

#define SECTOR_SIZE 512
#define BLOCK_SIZE_IN_SECTORS 16
#define BLOCK_SIZE  (SECTOR_SIZE * BLOCK_SIZE_IN_SECTORS)

#define CACHE_INVALID (~((uint64_t)0))

static uint8_t *cache        = NULL;
static uint64_t cached_block = CACHE_INVALID;

struct dap {
    uint16_t size;
    uint16_t count;
    uint16_t offset;
    uint16_t segment;
    uint64_t lba;
};

static struct dap *dap = NULL;

static int cache_block(int drive, uint64_t block) {
    if (block == cached_block)
        return 0;

    if (!dap) {
        dap = conv_mem_alloc(sizeof(struct dap));
        dap->size  = 16;
        dap->count = BLOCK_SIZE_IN_SECTORS;
    }

    if (!cache)
        cache = conv_mem_alloc_aligned(BLOCK_SIZE, 16);

    dap->segment = rm_seg(cache);
    dap->offset  = rm_off(cache);
    dap->lba     = block * BLOCK_SIZE_IN_SECTORS;

    struct rm_regs r = {0};
    r.eax = 0x4200;
    r.edx = drive;
    r.esi = (uint32_t)rm_off(dap);
    r.ds  = rm_seg(dap);

    rm_int(0x13, &r, &r);

    if (r.eflags & EFLAGS_CF) {
        int ah = (r.eax >> 8) & 0xff;
        panic("Disk error %x. Drive %x, LBA %x.\n", ah, drive, dap->lba);
        cached_block = CACHE_INVALID;
        return ah;
    }

    cached_block = block;

    return 0;
}

int read(int drive, void *buffer, uint64_t loc, uint64_t count) {
    uint64_t progress = 0;
    while (progress < count) {
        uint64_t block = (loc + progress) / BLOCK_SIZE;

        int ret;
        if ((ret = cache_block(drive, block)))
            return ret;

        uint64_t chunk = count - progress;
        uint64_t offset = (loc + progress) % BLOCK_SIZE;
        if (chunk > BLOCK_SIZE - offset)
            chunk = BLOCK_SIZE - offset;

        memcpy(buffer + progress, &cache[offset], chunk);
        progress += chunk;
    }

    return 0;
}

int read_partition(int drive, struct part *part, void *buffer, uint64_t loc, uint64_t count) {
    return read(drive, buffer, loc + (part->first_sect * 512), count);
}

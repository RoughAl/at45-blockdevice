#ifndef _FRAGMENTATION_FLASH_AT45_H_
#define _FRAGMENTATION_FLASH_AT45_H_

#include "mbed.h"
#include "DestructableSPI.h"
#include "BlockDevice.h"
#include "AT45.h"
#include "mbed_debug.h"

#if !defined(AT45_BLOCK_DEVICE_DEBUG)
#define at45_debug(...) do {} while(0)
#else
#define at45_debug(...) debug(__VA_ARGS__)
#endif

enum at45_bd_error {
    BD_ERROR_NO_MEMORY          = -4002,
    BD_ERROR_NOT_INITIALIZED    = -4003,
};

class AT45BlockDevice : public BlockDevice {
public:
    AT45BlockDevice() : spi(SPI_MOSI, SPI_MISO, SPI_SCK, SPI_NSS), at45(&spi, SPI_NSS) {
        pagesize = at45.pagesize();
        totalsize = pagesize * at45.pages();
        last_page = 0xffffffff;
    }

    virtual ~AT45BlockDevice() {
        if (pagebuffer) free(pagebuffer);
    }

    virtual int init() {
        pagebuffer = (char*)calloc(pagesize, 1);
        if (!pagebuffer) {
            return BD_ERROR_NO_MEMORY;
        }

        return BD_ERROR_OK;
    }

    virtual int deinit() {
        spi.free();

        return BD_ERROR_OK;
    }

    virtual int program(const void *a_buffer, bd_addr_t addr, bd_size_t size) {
        if (!pagebuffer) return BD_ERROR_NOT_INITIALIZED;

        // Q: a 'global' pagebuffer makes this code not thread-safe...
        // is this a problem? don't really wanna malloc/free in every call

        char *buffer = (char*)a_buffer;

        at45_debug("[AT45] write addr=%lu size=%d\n", addr, size);

        // find the page
        size_t bytes_left = size;
        while (bytes_left > 0) {
            uint32_t page = addr / pagesize; // this gets auto-rounded
            uint32_t offset = addr % pagesize; // offset from the start of the pagebuffer
            uint32_t length = pagesize - offset; // number of bytes to write in this pagebuffer
            if (length > bytes_left) length = bytes_left; // don't overflow

            at45_debug("[AT45] writing to page=%lu, offset=%lu, length=%lu\n", page, offset, length);

            int r;

            // retrieve the page first, as we don't want to overwrite the full page
            if (last_page != page) {
                r = at45.read_page(pagebuffer, page);
                if (r != 0) return r;
            }

            // at45_debug("[AT45] pagebuffer of page %d is:\n", page);
            // for (size_t ix = 0; ix < pagesize; ix++) {
                // at45_debug("%02x ", pagebuffer[ix]);
            // }
            // at45_debug("\n");

            // now memcpy to the pagebuffer
            memcpy(pagebuffer + offset, buffer, length);

            // at45_debug("pagebuffer after memcpy is:\n", page);
            // for (size_t ix = 0; ix < pagesize; ix++) {
                // at45_debug("%02x ", pagebuffer[ix]);
            // }
            // at45_debug("\n");

            // and write back
            r = at45.write_page(pagebuffer, page);
            if (r != 0) return r;

            // change the page
            bytes_left -= length;
            addr += length;
            buffer += length;

            last_page = page;
        }

        return BD_ERROR_OK;
    }

    virtual int read(void *a_buffer, bd_addr_t addr, bd_size_t size) {
        if (!pagebuffer) return BD_ERROR_NOT_INITIALIZED;

        at45_debug("[AT45] read addr=%lu size=%d\n", addr, size);

        char *buffer = (char*)a_buffer;

        size_t bytes_left = size;
        while (bytes_left > 0) {
            uint32_t page = addr / pagesize; // this gets auto-rounded
            uint32_t offset = addr % pagesize; // offset from the start of the pagebuffer
            uint32_t length = pagesize - offset; // number of bytes to read in this pagebuffer
            if (length > bytes_left) length = bytes_left; // don't overflow

            at45_debug("[AT45] Reading from page=%lu, offset=%lu, length=%lu\n", page, offset, length);

            if (last_page != page) {
                int r = at45.read_page(pagebuffer, page);
                if (r != 0) return r;
            }

            // copy into the provided buffer
            memcpy(buffer, pagebuffer + offset, length);

            // change the page
            bytes_left -= length;
            addr += length;
            buffer += length;

            last_page = page;
        }

        return BD_ERROR_OK;
    }

    virtual int erase(bd_addr_t addr, bd_size_t size) {
        if (!pagebuffer) return BD_ERROR_NOT_INITIALIZED;

        at45_debug("[AT45] erase addr=%lu size=%d\n", addr, size);

        uint32_t start_page = addr / pagesize; // this gets auto-rounded
        uint32_t end_page = (addr + size) / pagesize;

        memset(pagebuffer, 0xff, pagesize);

        for (size_t ix = start_page; ix <= end_page; ix++) {
            int r = at45.write_page(pagebuffer, ix);
            if (r != 0) return r;
        }

        if (last_page >= start_page && last_page <= end_page) {
            last_page = 0xffffffff;
        }

        return BD_ERROR_OK;
    }

    virtual bd_size_t get_read_size() const {
        return pagesize;
    }

    virtual bd_size_t get_program_size() const {
        return pagesize;
    }

    virtual bd_size_t get_erase_size() const {
        return pagesize;
    }

    virtual bd_size_t size() const {
        return totalsize;
    }

private:
    DestructableSPI  spi;
    AT45 at45;
    bd_size_t pagesize;
    bd_size_t totalsize;
    char* pagebuffer;
    uint32_t last_page;
};

#endif // _FRAGMENTATION_FLASH_AT45_H_

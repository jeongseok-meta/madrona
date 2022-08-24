#pragma once

#include <cstdlib>

namespace madrona {

class VirtualRegion {
public:
    VirtualRegion(uint64_t max_chunks, uint64_t init_chunks);
    ~VirtualRegion();

    inline void *ptr() const { return base_; }
    void commit(uint64_t start_chunk, uint64_t num_chunks);
    void decommit(uint64_t start_chunk, uint64_t num_chunks);

    static constexpr uint64_t chunkSize() { return 1 << chunkShift(); }

private:
    static constexpr uint64_t chunkShift() { return 16; }

    char *base_;
    uint64_t total_size_;
};

class VirtualStore {
public:
    VirtualStore(uint32_t bytes_per_item, uint32_t max_items);

    inline void * operator[](uint32_t idx) 
    {
        return (char *)region_.ptr() + bytes_per_item_ * idx;
    }

    inline const void * operator[](uint32_t idx) const
    {
        return (char *)region_.ptr() + bytes_per_item_ * idx;
    }

    void expand(uint32_t num_items);
    void shrink(uint32_t num_items);

    inline void * data() const { return region_.ptr(); }

    inline uint32_t numBytesPerItem() const { return bytes_per_item_; }

private:
    VirtualRegion region_;
    uint32_t bytes_per_item_;
    uint32_t committed_chunks_;
    uint32_t committed_items_;
};

}

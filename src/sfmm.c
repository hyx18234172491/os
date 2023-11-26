/**
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "sfmm.h"

#define SF_MM_PER_M 1024
#define SF_MM_MIN_FRAGMENT 32
#define SF_MM_ALIGNMENT 16
#define SF_MM_ALIGN(size) (((size) + (SF_MM_ALIGNMENT - 1)) & ~0xF)
#define SF_MM_HEAP_START_OFFSET 8

int is_init = 0;

void initFreeListHeader()
{
    for (int i = 0; i < NUM_FREE_LISTS; i++)
    {
        sf_free_list_heads[i].body.links.prev = &sf_free_list_heads[i];
        sf_free_list_heads[i].body.links.next = &sf_free_list_heads[i];
    }
}

static size_t getFreeSlot(size_t msize)
{
    if (msize <= SF_MM_PER_M)
        return 0;
    if (msize <= 2 * SF_MM_PER_M)
        return 1;
    if (msize <= 3 * SF_MM_PER_M)
        return 2;
    if (msize <= 5 * SF_MM_PER_M)
        return 3;
    if (msize <= 8 * SF_MM_PER_M)
        return 4;
    if (msize <= 13 * SF_MM_PER_M)
        return 5;
    if (msize <= 21 * SF_MM_PER_M)
        return 6;
    if (msize <= 34 * SF_MM_PER_M)
        return 7;
    if (msize <= 55 * SF_MM_PER_M)
        return 8;

    return NUM_FREE_LISTS - 1;
}

size_t getBlockSize(sf_header header)
{
    size_t block_size = header & 0xFFFFFFF0;
    return block_size << 4;
}

size_t getBlockSize_2(sf_block *block)
{
    sf_header header = block->header;

    size_t block_size = header & 0xFFFFFFF0;
    return block_size << 4;
}

sf_block *findFreeBlock(size_t align_size)
{
    size_t blocksize = getBlockSizeByAlignSize(align_size); // 根据payload size得到Block size
    size_t slot_id = getFreeSlot(align_size);
    sf_block *next = &sf_free_list_heads[slot_id];

    for (; slot_id < NUM_FREE_LISTS; slot_id++)
    {
        next = next->body.links.next;
        size_t free_blocksize = getBlockSize(next->header);

        while (free_blocksize < blocksize)
        {
            next = next->body.links.next;
            size_t free_blocksize = getBlockSize(next->header);
            if (free_blocksize > blocksize)
            {
                return next;
            }
        }
    }
    return NULL;
}

// 10000

void setBlockHeaderFooter(sf_block *block, size_t payloadsize)
{
    size_t block_size = getBlockSizeByPayloadSize(payloadsize);
    sf_footer pre_footer = block->prev_footer;
    // ((prv_alloc >> 2 )& 0x1)
    block->header = (pre_footer & 0x4) | 0x8 | block_size << 4 | payloadsize << 32;
    sf_footer *footer = block + sizeof(sf_header) + payloadsize;
    *footer = (pre_footer & 0x4) | 0x8 | block_size << 4 | payloadsize << 32;
    return;
}

void putBlockToSlot(sf_block *after_merge_block)
{
    size_t block_size = getBlockSize(after_merge_block->header);
    size_t slot = getFreeSlot(block_size);

    sf_block *slot_header = &sf_free_list_heads[slot];

    sf_block *origin_next = slot_header->body.links.next;

    setFreeBlockNext(slot_header, after_merge_block);
    setFreeBlockNext(after_merge_block, origin_next);

    setFreeBlockPrev(after_merge_block, slot_header);
    setFreeBlockPrev(origin_next, after_merge_block);
}

sf_block *mergePreFreeBlock(sf_block *left_free_block, sf_block *pre_block)
{
    size_t left_free_block_size = getBlockSize(left_free_block->header);
    size_t pre_block_size = getBlockSize(pre_block->header);

    size_t merge_block_size = left_free_block_size + left_free_block_size;

    size_t merge_payload_size = getPayloadSizeByBlockSize(merge_block_size); // 通过blocksize设置payload size
    setBlockHeaderFooter(pre_block, merge_payload_size);
    return pre_block;
}

sf_block *mergeFreeBlock(sf_block *left_free_block)
{
    sf_block *next_block = getNextBlock(left_free_block); // 获取下一个block
    sf_block *pre_block = getPreBlock(left_free_block);   // 获取前一个block

    char next_alloc = getAlloc(next_block); // 获取分配情况，是否是空闲的
    char pre_alloc = getAlloc(pre_block);

    sf_block *result_block;
    if (next_alloc && pre_alloc) // 前后
    {
        putBlockToSlot(left_free_block);
        result_block = left_free_block;
    }
    else if (next_alloc && (!pre_alloc)) // 前边空闲，后边已分配
    {
        result_block = mergePreFreeBlock(left_free_block, pre_block);
    }
    else if (!(next_alloc) && pre_alloc)
    {
        result_block = mergeNextFreeBlock(left_free_block, next_alloc);
    }
    else
    {
        result_block = mergePreNextFreeBlock(left_free_block, next_alloc);
    }
    return result_block;
}

size_t blockSizeToPayloadSize(size_t blocksize)
{
    return blocksize - sizeof(sf_header) << 1;
}

void processFoundBlock(sf_block *found_block, size_t align_size)
{
    // size_t found_block_blocksize = getBlockSize(found_block);    这两种形式都是可以的
    size_t found_block_blocksize = getBlockSize(found_block->header);
    size_t allo_block_size = align_size + getHeaderFooterSize();

    size_t left_size = found_block_blocksize - allo_block_size;

    if (left_size < SF_MM_MIN_FRAGMENT)
    {
        // 剩余的不需要处理了，
        // 只需要设置当前的要分配给用户的块就可以了
        size_t payload_size = blockSizeToPayloadSize(found_block_blocksize);
        setBlockHeaderFooter(found_block, payload_size); // 这里需要是空闲块的size
        return (char *)found_block + getHeaderFooterSize();
        return found_block->body.payload;
    }
    else
    {
        // 切割
        //      分配给用户的-和上边一样
        //      空余
        //          - 根据空余大小，找到合适的槽放进去（还涉及到内存合并的问题）
        setBlockHeaderFooter(found_block, align_size);
        sf_block *left_free_block = (char *)found_block + getHeaderFooterSize() + align_size;

        size_t left_free_alloc_size = left_size - getHeaderFooterSize();

        setBlockHeaderFooter(left_free_block, left_free_alloc_size); // 设置剩余空闲块的header以及footer

        setFreeBlockNext(left_free_block, NULL);
        setFreeBlockPrev(left_free_block, NULL);

        // 将其放到空闲槽中
        // 先合并，再放到槽中
        // sf_block *after_merge = mergeFreeBlock(left_free_block);

        // 放到槽中
        putBlockToSlot(left_free_block);
    }
}

void setPrelogeHeaderFooter(char *preloge){
    sf_header * preloge_header = (sf_header*)preloge;
    *preloge_header = 8;
    sf_footer * footer = (sf_footer *)(preloge + sizeof(sf_header) + 2*8); // 我们认为每一个next指针大小是8个字节
    *footer = 8;
}

void setEpilogeHeader(char *epiloge){
    sf_header* epiloge_header = (sf_header*)epiloge;
    *epiloge_header = 8;
}

void initPage(){
    char * page = (char*)(sf_mem_grow());
    if(page==NULL){
        return;
    }
    // 设置序言
    char *preloge = (page + SF_MM_HEAP_START_OFFSET);
    setPrelogeHeaderFooter(preloge);
    // 设置尾声
    char *heap_end = (char*)(sf_mem_end());
    char *epiloge = heap_end - sizeof(sf_header);
    setEpilogeHeader(epiloge);
    
}


void initProcess()
{
    if(is_init==0){
        initFreeListHeader();
        initPage();
    }
}

void *sf_malloc(size_t size)
{
    // To be implemented.
    /*
        1. sf_free_list_heads空闲列表的初始化
        2. size变成2字节对齐的形式，align_size
        3. 找到一块，满足align_size的空闲链表-返回一个地址
            - 切出去多余的内存
        4. 如果找不到，是不是应该去申请增加页page   - 返回一个地址

    */
    // 1. 第一步
    
    initProcess();
    // 2. 第二步
    size_t align_size = SF_MM_ALIGN(size);
    // 3. 第三步
    sf_block *found_block = findFreeBlock(align_size);
    if (found_block)
    {
        processFoundBlock(found_block, align_size);
    }
    else
    {
        // 4. 第4步
        found_block = addPagesAndProcessLeftPage();
        return found_block;
    }

    abort();
}

void sf_free(void *pp)
{
    // To be implemented.
    abort();
}

void *sf_realloc(void *pp, size_t rsize)
{
    // To be implemented.
    abort();
}

double sf_fragmentation()
{
    // To be implemented.
    abort();
}

double sf_utilization()
{
    // To be implemented.
    abort();
}

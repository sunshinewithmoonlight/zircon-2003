// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cksum.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <optional>
#include <utility>

#include <fs/journal/format.h>
#include <minfs/format.h>
#include <minfs/fsck.h>

#include "lib/fit/string_view.h"
#include "minfs-private.h"

namespace minfs {

namespace {

#ifdef __Fuchsia__
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::VmoStorage>;
#else
using RawBitmap = bitmap::RawBitmapGeneric<bitmap::DefaultStorage>;
#endif

enum class BlockType { DirectBlock = 0, IndirectBlock, DoubleIndirectBlock };

struct BlockInfo {
  ino_t owner;     // Inode number that maps this block.
  blk_t offset;    // Offset, in blocks, where this block is.
  BlockType type;  // What is this block used as.
};

const std::string kBlockInfoDirectStr("direct");
const std::string kBlockInfoIndirectStr("indirect");
const std::string kBlockInfoDoubleIndirectStr("double indirect");

// Given a type of block, returns human readable c-string for the block type.
std::string BlockTypeToString(BlockType type) {
  switch (type) {
    case BlockType::DirectBlock:
      return kBlockInfoDirectStr;
    case BlockType::IndirectBlock:
      return kBlockInfoIndirectStr;
    case BlockType::DoubleIndirectBlock:
      return kBlockInfoDoubleIndirectStr;
    default:
      ZX_ASSERT(false);
  }
}

// Returns the logical block accessed from the "indirect" structure within an inode.
// |direct| refers to the index within the indirect block.
blk_t LogicalBlockIndirect(blk_t indirect, blk_t direct = 0) {
  ZX_DEBUG_ASSERT(indirect < kMinfsIndirect);
  ZX_DEBUG_ASSERT(direct < kMinfsDirectPerIndirect);
  const blk_t start = kMinfsDirect;
  return start + (indirect * kMinfsDirectPerIndirect) + direct;
}
// Returns the logical block accessed from the "doubly indirect" structure within an inode.
// |indirect| refers to an index within the doubly_indirect block.
// |direct| refers to an index within |indirect|.
blk_t LogicalBlockDoublyIndirect(blk_t doubly_indirect, blk_t indirect = 0, blk_t direct = 0) {
  ZX_DEBUG_ASSERT(doubly_indirect < kMinfsDoublyIndirect);
  ZX_DEBUG_ASSERT(indirect < kMinfsDirectPerIndirect);
  ZX_DEBUG_ASSERT(direct < kMinfsDirectPerIndirect);
  const blk_t start = kMinfsDirect + (kMinfsIndirect * kMinfsDirectPerIndirect);
  return start + (kMinfsDirectPerDindirect * doubly_indirect) +
         (indirect * kMinfsDirectPerIndirect) + direct;
}

}  // namespace

class MinfsChecker {
 public:
  static zx_status_t Create(std::unique_ptr<Bcache> bc, Repair fsck_repair,
                            std::unique_ptr<MinfsChecker>* out);

  static std::unique_ptr<Bcache> Destroy(std::unique_ptr<MinfsChecker> checker) {
    return Minfs::Destroy(std::move(checker->fs_));
  }

  void CheckReserved();
  zx_status_t CheckInode(ino_t ino, ino_t parent, bool dot_or_dotdot);
  zx_status_t CheckUnlinkedInodes();
  zx_status_t CheckForUnusedBlocks() const;
  zx_status_t CheckForUnusedInodes() const;
  zx_status_t CheckLinkCounts() const;
  zx_status_t CheckAllocatedCounts() const;
  zx_status_t CheckSuperblockIntegrity() const;

  // "Set once"-style flag to identify if anything nonconforming
  // was found in the underlying filesystem -- even if it was fixed.
  bool conforming_;

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(MinfsChecker);

  MinfsChecker();
  zx_status_t GetInode(Inode* inode, ino_t ino);

  // Returns the nth block within an inode, relative to the start of the
  // file. Returns the "next_n" which might contain a bno. This "next_n"
  // is for performance reasons -- it allows fsck to avoid repeatedly checking
  // the same indirect / doubly indirect blocks with all internal
  // bno unallocated.
  zx_status_t GetInodeNthBno(Inode* inode, blk_t n, blk_t* next_n, blk_t* bno_out);
  zx_status_t CheckDirectory(Inode* inode, ino_t ino, ino_t parent, uint32_t flags);
  std::optional<std::string> CheckDataBlock(blk_t bno, BlockInfo block_info);
  zx_status_t CheckFile(Inode* inode, ino_t ino);

  std::unique_ptr<Minfs> fs_;
  RawBitmap checked_inodes_;
  RawBitmap checked_blocks_;

  // blk_info_ provides reverse lookup capability - a block number is mapped to
  // a set of BlockInfo. The filesystem is inconsistent if a block has more than
  // one <inode, offset, type>.
  std::map<blk_t, std::vector<BlockInfo>> blk_info_;

  uint32_t alloc_inodes_;
  uint32_t alloc_blocks_;
  fbl::Array<int32_t> links_;

  blk_t cached_doubly_indirect_;
  blk_t cached_indirect_;
  uint8_t doubly_indirect_cache_[kMinfsBlockSize];
  uint8_t indirect_cache_[kMinfsBlockSize];
};

zx_status_t MinfsChecker::GetInode(Inode* inode, ino_t ino) {
  if (ino >= fs_->Info().inode_count) {
    FS_TRACE_ERROR("check: ino %u out of range (>=%u)\n", ino, fs_->Info().inode_count);
    return ZX_ERR_OUT_OF_RANGE;
  }

  fs_->GetInodeManager()->Load(ino, inode);
  if ((inode->magic != kMinfsMagicFile) && (inode->magic != kMinfsMagicDir)) {
    FS_TRACE_ERROR("check: ino %u has bad magic %#x\n", ino, inode->magic);
    return ZX_ERR_IO_DATA_INTEGRITY;
  }
  return ZX_OK;
}

#define CD_DUMP 1
#define CD_RECURSE 2

zx_status_t MinfsChecker::GetInodeNthBno(Inode* inode, blk_t n, blk_t* next_n, blk_t* bno_out) {
  // The default value for the "next n". It's easier to set it here anyway,
  // since we proceed to modify n in the code below.
  *next_n = n + 1;
  if (n < kMinfsDirect) {
    *bno_out = inode->dnum[n];
    return ZX_OK;
  }

  n -= kMinfsDirect;
  uint32_t i = n / kMinfsDirectPerIndirect;  // indirect index
  uint32_t j = n % kMinfsDirectPerIndirect;  // direct index

  if (i < kMinfsIndirect) {
    blk_t ibno;
    if ((ibno = inode->inum[i]) == 0) {
      *bno_out = 0;
      *next_n = kMinfsDirect + (i + 1) * kMinfsDirectPerIndirect;
      return ZX_OK;
    }

    if (cached_indirect_ != ibno) {
      zx_status_t status;
      if ((status = fs_->ReadDat(ibno, indirect_cache_)) != ZX_OK) {
        return status;
      }
      cached_indirect_ = ibno;
    }

    uint32_t* ientry = reinterpret_cast<uint32_t*>(indirect_cache_);
    *bno_out = ientry[j];
    return ZX_OK;
  }

  n -= kMinfsIndirect * kMinfsDirectPerIndirect;
  i = n / (kMinfsDirectPerDindirect);  // doubly indirect index
  n -= (i * kMinfsDirectPerDindirect);
  j = n / kMinfsDirectPerIndirect;           // indirect index
  uint32_t k = n % kMinfsDirectPerIndirect;  // direct index

  if (i < kMinfsDoublyIndirect) {
    blk_t dibno;
    if ((dibno = inode->dinum[i]) == 0) {
      *bno_out = 0;
      *next_n = kMinfsDirect + kMinfsIndirect * kMinfsDirectPerIndirect +
                (i + 1) * kMinfsDirectPerDindirect;
      return ZX_OK;
    }

    if (cached_doubly_indirect_ != dibno) {
      zx_status_t status;
      if ((status = fs_->ReadDat(dibno, doubly_indirect_cache_)) != ZX_OK) {
        return status;
      }
      cached_doubly_indirect_ = dibno;
    }

    uint32_t* dientry = reinterpret_cast<uint32_t*>(doubly_indirect_cache_);
    blk_t ibno;
    if ((ibno = dientry[j]) == 0) {
      *bno_out = 0;
      *next_n = kMinfsDirect + kMinfsIndirect * kMinfsDirectPerIndirect +
                (i * kMinfsDirectPerDindirect) + (j + 1) * kMinfsDirectPerIndirect;
      return ZX_OK;
    }

    if (cached_indirect_ != ibno) {
      zx_status_t status;
      if ((status = fs_->ReadDat(ibno, indirect_cache_)) != ZX_OK) {
        return status;
      }
      cached_indirect_ = ibno;
    }

    uint32_t* ientry = reinterpret_cast<uint32_t*>(indirect_cache_);
    *bno_out = ientry[k];
    return ZX_OK;
  }

  return ZX_ERR_OUT_OF_RANGE;
}

zx_status_t MinfsChecker::CheckDirectory(Inode* inode, ino_t ino, ino_t parent, uint32_t flags) {
  unsigned eno = 0;
  bool dot = false;
  bool dotdot = false;
  uint32_t dirent_count = 0;

  zx_status_t status;
  fbl::RefPtr<VnodeMinfs> vn;
  VnodeMinfs::Recreate(fs_.get(), ino, &vn);

  size_t off = 0;
  while (true) {
    uint32_t data[MINFS_DIRENT_SIZE];
    size_t actual;
    status = vn->ReadInternal(nullptr, data, MINFS_DIRENT_SIZE, off, &actual);
    if (status != ZX_OK || actual != MINFS_DIRENT_SIZE) {
      FS_TRACE_ERROR("check: ino#%u: Could not read de[%u] at %zd\n", eno, ino, off);
      if (inode->dirent_count >= 2 && inode->dirent_count == eno - 1) {
        // So we couldn't read the last direntry, for whatever reason, but our
        // inode says that we shouldn't have been able to read it anyway.
        FS_TRACE_ERROR("check: de count (%u) > inode_dirent_count (%u)\n", eno,
                       inode->dirent_count);
      }
      return status != ZX_OK ? status : ZX_ERR_IO;
    }
    Dirent* de = reinterpret_cast<Dirent*>(data);
    uint32_t rlen = static_cast<uint32_t>(MinfsReclen(de, off));
    uint32_t dlen = DirentSize(de->namelen);
    bool is_last = de->reclen & kMinfsReclenLast;
    if (!is_last && ((rlen < MINFS_DIRENT_SIZE) || (dlen > rlen) || (dlen > kMinfsMaxDirentSize) ||
                     (rlen & 3))) {
      FS_TRACE_ERROR("check: ino#%u: de[%u]: bad dirent reclen (%u)\n", ino, eno, rlen);
      return ZX_ERR_IO_DATA_INTEGRITY;
    }
    if (de->ino == 0) {
      if (flags & CD_DUMP) {
        FS_TRACE_DEBUG("ino#%u: de[%u]: <empty> reclen=%u\n", ino, eno, rlen);
      }
    } else {
      // Re-read the dirent to acquire the full name
      uint32_t record_full[DirentSize(NAME_MAX)];
      status = vn->ReadInternal(nullptr, record_full, DirentSize(de->namelen), off, &actual);
      if (status != ZX_OK || actual != DirentSize(de->namelen)) {
        FS_TRACE_ERROR("check: Error reading dirent of size: %u\n", DirentSize(de->namelen));
        return ZX_ERR_IO;
      }
      de = reinterpret_cast<Dirent*>(record_full);
      bool dot_or_dotdot = false;

      if ((de->namelen == 0) || (de->namelen > (rlen - MINFS_DIRENT_SIZE))) {
        FS_TRACE_ERROR("check: ino#%u: de[%u]: invalid namelen %u\n", ino, eno, de->namelen);
        return ZX_ERR_IO_DATA_INTEGRITY;
      }
      if ((de->namelen == 1) && (de->name[0] == '.')) {
        if (dot) {
          FS_TRACE_ERROR("check: ino#%u: multiple '.' entries\n", ino);
        }
        dot_or_dotdot = true;
        dot = true;
        if (de->ino != ino) {
          FS_TRACE_ERROR("check: ino#%u: de[%u]: '.' ino=%u (not self!)\n", ino, eno, de->ino);
        }
      }
      if ((de->namelen == 2) && (de->name[0] == '.') && (de->name[1] == '.')) {
        if (dotdot) {
          FS_TRACE_ERROR("check: ino#%u: multiple '..' entries\n", ino);
        }
        dot_or_dotdot = true;
        dotdot = true;
        if (de->ino != parent) {
          FS_TRACE_ERROR("check: ino#%u: de[%u]: '..' ino=%u (not parent (ino#%u)!)\n", ino, eno,
                         de->ino, parent);
        }
      }
      // TODO: check for cycles (non-dot/dotdot dir ref already in checked bitmap)
      if (flags & CD_DUMP) {
        FS_TRACE_DEBUG("ino#%u: de[%u]: ino=%u type=%u '%.*s' %s\n", ino, eno, de->ino, de->type,
                       de->namelen, de->name, is_last ? "[last]" : "");
      }

      if (flags & CD_RECURSE) {
        if ((status = CheckInode(de->ino, ino, dot_or_dotdot)) < 0) {
          return status;
        }
      }
      dirent_count++;
    }
    if (is_last) {
      break;
    } else {
      off += rlen;
    }
    eno++;
  }
  if (dirent_count != inode->dirent_count) {
    FS_TRACE_ERROR("check: ino#%u: dirent_count of %u != %u (actual)\n", ino, inode->dirent_count,
                   dirent_count);
  }
  if (dot == false) {
    FS_TRACE_ERROR("check: ino#%u: directory missing '.'\n", ino);
  }
  if (dotdot == false) {
    FS_TRACE_ERROR("check: ino#%u: directory missing '..'\n", ino);
  }
  return ZX_OK;
}

std::optional<std::string> MinfsChecker::CheckDataBlock(blk_t bno, BlockInfo block_info) {
  if (bno == 0) {
    return std::string("reserved bno");
  }
  if (bno >= fs_->Info().block_count) {
    return std::string("out of range");
  }
  if (!fs_->GetBlockAllocator()->CheckAllocated(bno)) {
    return std::string("not allocated");
  }
  if (checked_blocks_.Get(bno, bno + 1)) {
    auto entries = blk_info_[bno].size();
    // The entries are printed as
    // "double-allocated"
    // "  <ino: 4294967295, off: 4294967295 type: DI>\n"
    std::string str("double-allocated\n");
    for (size_t i = 0; i < entries; i++) {
      str.append("  <ino: " + std::to_string(blk_info_[bno][i].owner) +
                 ", off: " + std::to_string(blk_info_[bno][i].offset) +
                 " type: " + BlockTypeToString(blk_info_[bno][i].type) + ">\n");
    }
    blk_info_[bno].push_back(block_info);
    return str;
  }
  checked_blocks_.Set(bno, bno + 1);
  std::vector<BlockInfo> vec;
  vec.push_back(block_info);
  blk_info_.insert(std::pair<blk_t, std::vector<BlockInfo>>(bno, vec));
  alloc_blocks_++;
  return std::nullopt;
}

zx_status_t MinfsChecker::CheckFile(Inode* inode, ino_t ino) {
  FS_TRACE_DEBUG("Direct blocks: \n");
  for (unsigned n = 0; n < kMinfsDirect; n++) {
    FS_TRACE_DEBUG(" %d,", inode->dnum[n]);
  }
  FS_TRACE_DEBUG(" ...\n");

  uint32_t block_count = 0;

  // count and sanity-check indirect blocks
  for (unsigned n = 0; n < kMinfsIndirect; n++) {
    if (inode->inum[n]) {
      BlockInfo block_info = {ino, LogicalBlockIndirect(n), BlockType::IndirectBlock};
      auto msg = CheckDataBlock(inode->inum[n], block_info);
      if (msg) {
        FS_TRACE_WARN("check: ino#%u: indirect block %u(@%u): %s\n", ino, n, inode->inum[n],
                      msg.value().c_str());
        conforming_ = false;
      }
      block_count++;
    }
  }

  // count and sanity-check doubly indirect blocks
  for (unsigned n = 0; n < kMinfsDoublyIndirect; n++) {
    if (inode->dinum[n]) {
      BlockInfo block_info = {ino, LogicalBlockDoublyIndirect(n), BlockType::DoubleIndirectBlock};
      auto msg = CheckDataBlock(inode->dinum[n], block_info);
      if (msg) {
        FS_TRACE_WARN("check: ino#%u: doubly indirect block %u(@%u): %s\n", ino, n, inode->dinum[n],
                      msg.value().c_str());
        conforming_ = false;
      }
      block_count++;

      char data[kMinfsBlockSize];
      zx_status_t status;
      if ((status = fs_->ReadDat(inode->dinum[n], data)) != ZX_OK) {
        return status;
      }
      uint32_t* entry = reinterpret_cast<uint32_t*>(data);

      for (unsigned m = 0; m < kMinfsDirectPerIndirect; m++) {
        if (entry[m]) {
          BlockInfo block_info = {ino, LogicalBlockDoublyIndirect(n, m), BlockType::IndirectBlock};
          msg = CheckDataBlock(entry[m], block_info);
          if (msg) {
            FS_TRACE_WARN("check: ino#%u: indirect block (in dind) %u(@%u): %s\n", ino, m, entry[m],
                          msg.value().c_str());
            conforming_ = false;
          }
          block_count++;
        }
      }
    }
  }

  // count and sanity-check data blocks

  // The next block which would be allocated if we expand the file size
  // by a single block.
  unsigned next_blk = 0;
  cached_doubly_indirect_ = 0;
  cached_indirect_ = 0;

  blk_t n = 0;
  while (true) {
    zx_status_t status;
    blk_t bno;
    blk_t next_n;
    if ((status = GetInodeNthBno(inode, n, &next_n, &bno)) < 0) {
      if (status == ZX_ERR_OUT_OF_RANGE) {
        break;
      } else {
        return status;
      }
    }
    assert(next_n > n);
    if (bno) {
      next_blk = n + 1;
      block_count++;
      BlockInfo block_info = {ino, n, BlockType::DirectBlock};
      auto msg = CheckDataBlock(bno, block_info);
      if (msg) {
        FS_TRACE_WARN("check: ino#%u: block %u(@%u): %s\n", ino, n, bno, msg.value().c_str());
        conforming_ = false;
      }
    }
    n = next_n;
  }
  if (next_blk) {
    unsigned max_blocks = fbl::round_up(inode->size, kMinfsBlockSize) / kMinfsBlockSize;
    if (next_blk > max_blocks) {
      FS_TRACE_WARN("check: ino#%u: filesize too small\n", ino);
      conforming_ = false;
    }
  }
  if (block_count != inode->block_count) {
    FS_TRACE_WARN("check: ino#%u: block count %u, actual blocks %u\n", ino, inode->block_count,
                  block_count);
    conforming_ = false;
  }
  return ZX_OK;
}

void MinfsChecker::CheckReserved() {
  // Check reserved inode '0'.
  if (fs_->GetInodeManager()->GetInodeAllocator()->CheckAllocated(0)) {
    checked_inodes_.Set(0, 1);
    alloc_inodes_++;
  } else {
    FS_TRACE_WARN("check: reserved inode#0: not marked in-use\n");
    conforming_ = false;
  }

  // Check reserved data block '0'.
  if (fs_->GetBlockAllocator()->CheckAllocated(0)) {
    checked_blocks_.Set(0, 1);
    alloc_blocks_++;
  } else {
    FS_TRACE_WARN("check: reserved block#0: not marked in-use\n");
    conforming_ = false;
  }
}

zx_status_t MinfsChecker::CheckInode(ino_t ino, ino_t parent, bool dot_or_dotdot) {
  Inode inode;
  zx_status_t status;

  if ((status = GetInode(&inode, ino)) < 0) {
    FS_TRACE_ERROR("check: ino#%u: not readable: %d\n", ino, status);
    return status;
  }

  bool prev_checked = checked_inodes_.Get(ino, ino + 1);

  if (inode.magic == kMinfsMagicDir && prev_checked && !dot_or_dotdot) {
    FS_TRACE_ERROR(
        "check: ino#%u: Multiple hard links to directory (excluding '.' and '..') found\n", ino);
    return ZX_ERR_BAD_STATE;
  }

  links_[ino - 1] += 1;

  if (prev_checked) {
    // we've been here before
    return ZX_OK;
  }

  links_[ino - 1] -= inode.link_count;
  checked_inodes_.Set(ino, ino + 1);
  alloc_inodes_++;

  if (!fs_->GetInodeManager()->GetInodeAllocator()->CheckAllocated(ino)) {
    FS_TRACE_WARN("check: ino#%u: not marked in-use\n", ino);
    conforming_ = false;
  }

  if (inode.magic == kMinfsMagicDir) {
    FS_TRACE_DEBUG("ino#%u: DIR blks=%u links=%u\n", ino, inode.block_count, inode.link_count);
    if ((status = CheckFile(&inode, ino)) < 0) {
      return status;
    }
    if ((status = CheckDirectory(&inode, ino, parent, CD_DUMP)) < 0) {
      return status;
    }
    if ((status = CheckDirectory(&inode, ino, parent, CD_RECURSE)) < 0) {
      return status;
    }
  } else {
    FS_TRACE_DEBUG("ino#%u: FILE blks=%u links=%u size=%u\n", ino, inode.block_count,
                   inode.link_count, inode.size);
    if ((status = CheckFile(&inode, ino)) < 0) {
      return status;
    }
  }
  return ZX_OK;
}

zx_status_t MinfsChecker::CheckUnlinkedInodes() {
  ino_t last_ino = 0;
  ino_t next_ino = fs_->Info().unlinked_head;
  ino_t unlinked_count = 0;

  while (next_ino != 0) {
    unlinked_count++;

    Inode inode;
    zx_status_t status = GetInode(&inode, next_ino);
    if (status != ZX_OK) {
      FS_TRACE_ERROR("check: ino#%u: not readable: %d\n", next_ino, status);
      return status;
    }

    if (inode.link_count > 0) {
      FS_TRACE_ERROR("check: ino#%u: should have 0 links\n", next_ino);
      return ZX_ERR_BAD_STATE;
    }

    if (inode.last_inode != last_ino) {
      FS_TRACE_ERROR("check: ino#%u: incorrect last unlinked inode\n", next_ino);
      return ZX_ERR_BAD_STATE;
    }

    links_[next_ino - 1] = -1;

    if ((status = CheckInode(next_ino, 0, 0)) != ZX_OK) {
      FS_TRACE_ERROR("minfs_check: CheckInode failure: %d\n", status);
      return status;
    }

    last_ino = next_ino;
    next_ino = inode.next_inode;
  }

  if (fs_->Info().unlinked_tail != last_ino) {
    FS_TRACE_ERROR("minfs_check: Incorrect unlinked tail: %d\n", fs_->Info().unlinked_tail);
    return ZX_ERR_BAD_STATE;
  }

  if (unlinked_count > 0) {
    FS_TRACE_WARN("minfs_check: Warning: %u unlinked inodes found\n", unlinked_count);
  }

  return ZX_OK;
}

zx_status_t MinfsChecker::CheckForUnusedBlocks() const {
  unsigned missing = 0;

  for (unsigned n = 0; n < fs_->Info().block_count; n++) {
    if (fs_->GetBlockAllocator()->CheckAllocated(n)) {
      if (!checked_blocks_.Get(n, n + 1)) {
        missing++;
      }
    }
  }
  if (missing) {
    FS_TRACE_ERROR("check: %u allocated block%s not in use\n", missing, missing > 1 ? "s" : "");
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

zx_status_t MinfsChecker::CheckForUnusedInodes() const {
  unsigned missing = 0;
  for (unsigned n = 0; n < fs_->Info().inode_count; n++) {
    if (fs_->GetInodeManager()->GetInodeAllocator()->CheckAllocated(n)) {
      if (!checked_inodes_.Get(n, n + 1)) {
        missing++;
      }
    }
  }
  if (missing) {
    FS_TRACE_ERROR("check: %u allocated inode%s not in use\n", missing, missing > 1 ? "s" : "");
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

zx_status_t MinfsChecker::CheckLinkCounts() const {
  unsigned error = 0;
  for (uint32_t n = 0; n < fs_->Info().inode_count; n++) {
    if (links_[n] != 0) {
      error += 1;
      FS_TRACE_ERROR("check: inode#%u has incorrect link count %u\n", n + 1, links_[n]);
      return ZX_ERR_BAD_STATE;
    }
  }
  if (error) {
    FS_TRACE_ERROR("check: %u inode%s with incorrect link count\n", error, error > 1 ? "s" : "");
    return ZX_ERR_BAD_STATE;
  }
  return ZX_OK;
}

zx_status_t MinfsChecker::CheckAllocatedCounts() const {
  zx_status_t status = ZX_OK;
  if (alloc_blocks_ != fs_->Info().alloc_block_count) {
    FS_TRACE_ERROR("check: incorrect allocated block count %u (should be %u)\n",
                   fs_->Info().alloc_block_count, alloc_blocks_);
    status = ZX_ERR_BAD_STATE;
  }

  if (alloc_inodes_ != fs_->Info().alloc_inode_count) {
    FS_TRACE_ERROR("check: incorrect allocated inode count %u (should be %u)\n",
                   fs_->Info().alloc_inode_count, alloc_inodes_);
    status = ZX_ERR_BAD_STATE;
  }

  return status;
}

zx_status_t MinfsChecker::CheckSuperblockIntegrity() const {
  char data[kMinfsBlockSize];
  blk_t journal_block;

#ifdef __Fuchsia__
  journal_block = static_cast<blk_t>(JournalStartBlock(fs_->Info()));
#else
  journal_block = fs_->GetBlockOffsets().JournalStartBlock();
#endif

  if (fs_->bc_->Readblk(journal_block, data) < 0) {
    FS_TRACE_ERROR("minfs: could not read journal block\n");
    return ZX_ERR_IO;
  }

  // Check that the journal superblock is valid.
  fs::JournalInfo* journal_info = reinterpret_cast<fs::JournalInfo*>(data);
  if (journal_info->magic != fs::kJournalMagic) {
    FS_TRACE_ERROR("minfs: invalid journal magic\n");
    return ZX_ERR_BAD_STATE;
  }

  uint32_t old_checksum = journal_info->checksum;
  journal_info->checksum = 0;
  journal_info->checksum = crc32(0, reinterpret_cast<uint8_t*>(data), sizeof(fs::JournalInfo));
  if (journal_info->checksum != old_checksum) {
    FS_TRACE_ERROR("minfs: invalid journal checksum\n");
    return ZX_ERR_BAD_STATE;
  }

  // Check that the backup superblock is valid.
  blk_t backup_location;
  if ((fs_->Info().flags & kMinfsFlagFVM) == 0) {
    backup_location = kNonFvmSuperblockBackup;
  } else {
#ifdef __Fuchsia__
    backup_location = kFvmSuperblockBackup;
#else
    backup_location = fs_->GetBlockOffsets().IntegrityStartBlock();
#endif
  }

  if (fs_->bc_->Readblk(backup_location, data) < 0) {
    FS_TRACE_ERROR("minfs: could not read backup superblock\n");
    return ZX_ERR_IO;
  }

  Superblock* backup_info = reinterpret_cast<Superblock*>(data);
#ifdef __Fuchsia__
  return CheckSuperblock(backup_info, fs_->bc_->device(), fs_->bc_->Maxblk());
#else
  return CheckSuperblock(backup_info, fs_->bc_->Maxblk());
#endif
}

MinfsChecker::MinfsChecker()
    : conforming_(true), fs_(nullptr), alloc_inodes_(0), alloc_blocks_(0), links_() {}

zx_status_t MinfsChecker::Create(std::unique_ptr<Bcache> bc, Repair fsck_repair,
                                 std::unique_ptr<MinfsChecker>* out) {
  MountOptions options = {};
  if (fsck_repair == Repair::kEnabled) {
    options.readonly_after_initialization = false;
    options.repair_filesystem = true;
    options.use_journal = true;
  } else {
    options.readonly_after_initialization = false;
    options.repair_filesystem = false;
    options.use_journal = false;
  }
  std::unique_ptr<Minfs> fs;
  zx_status_t status = Minfs::Create(std::move(bc), options, &fs);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("MinfsChecker::Create Failed to Create Minfs: %d\n", status);
    return status;
  }

  const Superblock& info = fs->Info();

  auto checker = std::unique_ptr<MinfsChecker>(new MinfsChecker());
  checker->links_.reset(new int32_t[info.inode_count]{0}, info.inode_count);
  checker->links_[0] = -1;
  checker->cached_doubly_indirect_ = 0;
  checker->cached_indirect_ = 0;

  if ((status = checker->checked_inodes_.Reset(info.inode_count)) != ZX_OK) {
    FS_TRACE_ERROR("MinfsChecker::Init Failed to reset checked inodes: %d\n", status);
    return status;
  }
  if ((status = checker->checked_blocks_.Reset(info.block_count)) != ZX_OK) {
    FS_TRACE_ERROR("MinfsChecker::Init Failed to reset checked blocks: %d\n", status);
    return status;
  }
  checker->fs_ = std::move(fs);
  *out = std::move(checker);
  return ZX_OK;
}

// Write Superblock and Backup Superblock to disk.
#ifdef __Fuchsia__
zx_status_t WriteSuperBlockAndBackupSuperblock(fs::TransactionHandler* transaction_handler,
                                               block_client::BlockDevice* device,
                                               Superblock* info) {
#else
zx_status_t WriteSuperBlockAndBackupSuperblock(fs::TransactionHandler* transaction_handler,
                                               Superblock* info) {
#endif
#ifdef __Fuchsia__
  zx::vmo vmo;
  fuchsia_hardware_block_VmoId vmoid;
  const size_t kVmoBlocks = 1;
  zx_status_t status = CreateAndRegisterVmo(device, &vmo, kVmoBlocks, &vmoid);
  if (status != ZX_OK) {
    return status;
  }
  // Prepare fifo transaction for write.
  status = vmo.write(info, 0, sizeof(*info));
  if (status != ZX_OK) {
    return status;
  }

  block_fifo_request_t request[2];

  const uint32_t disk_blocks_per_fs_block =
      kMinfsBlockSize / transaction_handler->DeviceBlockSize();
  request[0].opcode = BLOCKIO_WRITE;
  request[0].vmoid = vmoid.id;
  request[0].group = transaction_handler->BlockGroupID();
  request[0].length = disk_blocks_per_fs_block;
  request[0].vmo_offset = 0;
  request[0].dev_offset = kSuperblockStart * disk_blocks_per_fs_block;

  request[1].opcode = BLOCKIO_WRITE;
  request[1].vmoid = vmoid.id;
  request[1].group = transaction_handler->BlockGroupID();
  request[1].length = disk_blocks_per_fs_block;
  request[1].vmo_offset = 0;
  if ((info->flags & kMinfsFlagFVM) == 0) {
    request[1].dev_offset = kNonFvmSuperblockBackup * disk_blocks_per_fs_block;
  } else {
    request[1].dev_offset = kFvmSuperblockBackup * disk_blocks_per_fs_block;
  }
  status = device->FifoTransaction(request, 2);
  if (status != ZX_OK) {
    return status;
  }

  request[0].opcode = BLOCKIO_CLOSE_VMO;
  return (device->FifoTransaction(&request[0], 1));
#else
  zx_status_t status = transaction_handler->Writeblk(kSuperblockStart, info);
  if (status != ZX_OK) {
    return status;
  }
  if ((info->flags & kMinfsFlagFVM) == 0) {
    status = transaction_handler->Writeblk(kNonFvmSuperblockBackup, info);
  } else {
    status = transaction_handler->Writeblk(kFvmSuperblockBackup, info);
  }
  return status;
#endif
}

// Reads backup superblock from correct location depending on whether filesystem has FVM support.
#ifdef __Fuchsia__
zx_status_t ReadBackupSuperblock(fs::TransactionHandler* transaction_handler,
                                 block_client::BlockDevice* device, uint32_t max_blocks,
                                 uint32_t backup_location, Superblock* out_backup) {
  zx_status_t status = device->ReadBlock(backup_location, kMinfsBlockSize, out_backup);
  if (status != ZX_OK) {
    return status;
  }
  status = CheckSuperblock(out_backup, device, max_blocks);
  if (status != ZX_OK) {
    return status;
  }
  // Found a valid backup superblock. Confirm if the FVM flags are set in the backup superblock.
  if ((backup_location == kFvmSuperblockBackup) && ((out_backup->flags & kMinfsFlagFVM) == 0)) {
    return ZX_ERR_BAD_STATE;
  } else if ((backup_location == kNonFvmSuperblockBackup) &&
             ((out_backup->flags & kMinfsFlagFVM) != 0)) {
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}
#endif

// Repairs superblock from backup.
#ifdef __Fuchsia__
zx_status_t RepairSuperblock(fs::TransactionHandler* transaction_handler,
                             block_client::BlockDevice* device, uint32_t max_blocks,
                             Superblock* info_out) {
  Superblock backup_info;
  // Try the FVM backup location first.
  zx_status_t status = ReadBackupSuperblock(transaction_handler, device, max_blocks,
                                            kFvmSuperblockBackup, &backup_info);

  if (status != ZX_OK) {
    // Try the non-fvm backup superblock location.
    status = ReadBackupSuperblock(transaction_handler, device, max_blocks, kNonFvmSuperblockBackup,
                                  &backup_info);
  }

  if (status != ZX_OK) {
    FS_TRACE_ERROR("Fsck::RepairSuperblock failed. Unrepairable superblock: %d\n", status);
    return status;
  }
  FS_TRACE_INFO("Superblock corrupted. Repairing filesystem from backup superblock.\n");

  // Try to reconstruct alloc_*_counts of the backup superblock, since the
  // alloc_*_counts might be out-of-sync with the actual values.
  status = ReconstructAllocCounts(transaction_handler, device, &backup_info);

  if (status != ZX_OK) {
    FS_TRACE_ERROR("Fsck::ReconstructAllocCounts failed. Unrepairable superblock: %d\n", status);
    return status;
  }
  // Recalculate checksum.
  UpdateChecksum(&backup_info);

  // Update superblock and backup superblock.
  status = WriteSuperBlockAndBackupSuperblock(transaction_handler, device, &backup_info);

  if (status != ZX_OK) {
    FS_TRACE_ERROR("Fsck::RepairSuperblock failed to repair superblock from backup :%d", status);
  }

  // Updating in-memory info.
  memcpy(info_out, &backup_info, sizeof(backup_info));
  return status;
}
#endif

zx_status_t LoadSuperblock(Bcache* bc, Superblock* out_info) {
  zx_status_t status = bc->Readblk(kSuperblockStart, out_info);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("minfs: could not read info block.\n");
    return status;
  }
  DumpInfo(out_info);
#ifdef __Fuchsia__
  status = CheckSuperblock(out_info, bc->device(), bc->Maxblk());
#else
  status = CheckSuperblock(out_info, bc->Maxblk());
#endif
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Fsck: check_info failure: %d\n", status);
    return status;
  }
  return ZX_OK;
}

zx_status_t UsedDataSize(std::unique_ptr<Bcache>& bc, uint64_t* out_size) {
  zx_status_t status;
  Superblock info = {};
  if ((status = LoadSuperblock(bc.get(), &info)) != ZX_OK) {
    return status;
  }

  *out_size = (info.alloc_block_count * info.block_size);
  return ZX_OK;
}

zx_status_t UsedInodes(std::unique_ptr<Bcache>& bc, uint64_t* out_inodes) {
  zx_status_t status;
  Superblock info = {};
  if ((status = LoadSuperblock(bc.get(), &info)) != ZX_OK) {
    return status;
  }

  *out_inodes = info.alloc_inode_count;
  return ZX_OK;
}

zx_status_t UsedSize(std::unique_ptr<Bcache>& bc, uint64_t* out_size) {
  zx_status_t status;
  Superblock info = {};
  if ((status = LoadSuperblock(bc.get(), &info)) != ZX_OK) {
    return status;
  }

  *out_size = (NonDataBlocks(info) + info.alloc_block_count) * info.block_size;
  return ZX_OK;
}

#ifdef __Fuchsia__
zx_status_t CalculateBitsSetBitmap(fs::TransactionHandler* transaction_handler,
                                   block_client::BlockDevice* device, blk_t start_block,
                                   uint32_t num_blocks, uint32_t* out_bits_set) {
#else
zx_status_t CalculateBitsSetBitmap(fs::TransactionHandler* transaction_handler, blk_t start_block,
                                   uint32_t num_blocks, uint32_t* out_bits_set) {
#endif
  minfs::RawBitmap bitmap;
  zx_status_t status = bitmap.Reset(num_blocks * kMinfsBlockBits);
  if (status != ZX_OK) {
    return status;
  }
  fs::ReadTxn read_transaction(transaction_handler);
#ifdef __Fuchsia__
  fuchsia_hardware_block_VmoId map_vmoid;
  status = device->BlockAttachVmo(bitmap.StorageUnsafe()->GetVmo(), &map_vmoid);
  if (status != ZX_OK) {
    return status;
  }
  read_transaction.Enqueue(map_vmoid.id, 0, start_block, num_blocks);
#else
  read_transaction.Enqueue(bitmap.StorageUnsafe()->GetData(), 0, start_block, num_blocks);
#endif
  status = read_transaction.Transact();
  if (status != ZX_OK) {
    return status;
  }

  // Efficiently iterate through the bitmap to count the number of bits set in the bitmap.
  size_t off = 0;
  size_t bitmap_size = bitmap.size();
  size_t count = 0;

  while (off < bitmap_size) {
    size_t ind = 0;
    if (bitmap.Find(true, off, bitmap_size, 1, &ind) == ZX_OK) {
      size_t scan_ind = 0;
      if (bitmap.Scan(ind, bitmap_size, true, &scan_ind)) {
        count += (bitmap_size - ind);
        break;
      }
      count += (scan_ind - ind);
      off = scan_ind + 1;

    } else {
      break;
    }
  }

  *out_bits_set = static_cast<uint32_t>(count);
  return ZX_OK;
}

#ifdef __Fuchsia__
zx_status_t ReconstructAllocCounts(fs::TransactionHandler* transaction_handler,
                                   block_client::BlockDevice* device, Superblock* out_info) {
#else
zx_status_t ReconstructAllocCounts(fs::TransactionHandler* transaction_handler,
                                   Superblock* out_info) {
#endif
  uint32_t allocation_bitmap_num_blocks =
      (out_info->block_count + kMinfsBlockBits - 1) / kMinfsBlockBits;

#ifdef __Fuchsia__
  // Correct allocated block count.
  zx_status_t status =
      CalculateBitsSetBitmap(transaction_handler, device, out_info->abm_block,
                             allocation_bitmap_num_blocks, &(out_info->alloc_block_count));
#else
  zx_status_t status =
      CalculateBitsSetBitmap(transaction_handler, out_info->abm_block, allocation_bitmap_num_blocks,
                             &(out_info->alloc_block_count));
#endif
  if (status != ZX_OK) {
    return status;
  }
  uint32_t inode_bitmap_num_blocks =
      (out_info->inode_count + kMinfsBlockBits - 1) / kMinfsBlockBits;

#ifdef __Fuchsia__
  // Correct allocated inode count.
  status = CalculateBitsSetBitmap(transaction_handler, device, out_info->ibm_block,
                                  inode_bitmap_num_blocks, &(out_info->alloc_inode_count));
#else
  status = CalculateBitsSetBitmap(transaction_handler, out_info->ibm_block, inode_bitmap_num_blocks,
                                  &(out_info->alloc_inode_count));
#endif

  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx_status_t Fsck(std::unique_ptr<Bcache> bc, Repair fsck_repair, std::unique_ptr<Bcache>* out_bc) {
  std::unique_ptr<MinfsChecker> chk;
  zx_status_t status = MinfsChecker::Create(std::move(bc), fsck_repair, &chk);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Fsck: Init failure: %d\n", status);
    return status;
  }

  chk->CheckReserved();

  // TODO: check root not a directory
  status = chk->CheckInode(1, 1, 0);
  if (status != ZX_OK) {
    FS_TRACE_ERROR("Fsck: CheckInode failure: %d\n", status);
    return status;
  }

  zx_status_t r;

  // Save an error if it occurs, but check for subsequent errors anyway.
  r = chk->CheckUnlinkedInodes();
  status |= (status != ZX_OK) ? 0 : r;
  r = chk->CheckForUnusedBlocks();
  status |= (status != ZX_OK) ? 0 : r;
  r = chk->CheckForUnusedInodes();
  status |= (status != ZX_OK) ? 0 : r;
  r = chk->CheckLinkCounts();
  status |= (status != ZX_OK) ? 0 : r;
  r = chk->CheckAllocatedCounts();
  status |= (status != ZX_OK) ? 0 : r;

  r = chk->CheckSuperblockIntegrity();
  status |= (status != ZX_OK) ? 0 : r;

  // TODO: check allocated inodes that were abandoned
  // TODO: check allocated blocks that were not accounted for
  // TODO: check unallocated inodes where magic != 0
  status |= (status != ZX_OK) ? 0 : (chk->conforming_ ? ZX_OK : ZX_ERR_BAD_STATE);
  if (status != ZX_OK) {
    return status;
  }

  if (out_bc != nullptr) {
    *out_bc = MinfsChecker::Destroy(std::move(chk));
  }
  return ZX_OK;
}

}  // namespace minfs

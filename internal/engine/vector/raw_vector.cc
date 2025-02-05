/**
 * Copyright 2019 The Gamma Authors.
 *
 * This source code is licensed under the Apache License, Version 2.0 license
 * found in the LICENSE file in the root directory of this source tree.
 */

#include "raw_vector.h"

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "util/log.h"
#include "util/utils.h"

using namespace std;

namespace vearch {

RawVector::RawVector(VectorMetaInfo *meta_info, const string &root_path,
                     bitmap::BitmapManager *docids_bitmap,
                     const StoreParams &store_params)
    : VectorReader(meta_info),
      root_path_(root_path),
      total_mem_bytes_(0),
      store_params_(store_params),
      docids_bitmap_(docids_bitmap) {
  data_size_ = meta_info_->DataSize();
  vio_ = nullptr;
  vid_mgr_ = nullptr;
}

RawVector::~RawVector() { CHECK_DELETE(vid_mgr_); }

int RawVector::Init(std::string vec_name, bool multi_vids) {
  desc_ += "raw vector=" + meta_info_->Name() + ", ";
  if (multi_vids) {
    LOG(ERROR) << "multi-vids is unsupported now";
    return -1;
  }

  // vid2docid
  vid_mgr_ = new VIDMgr(multi_vids);
  vid_mgr_->Init(kInitSize, total_mem_bytes_);

  vector_byte_size_ = meta_info_->Dimension() * data_size_;

  if (InitStore(vec_name)) return -2;

  LOG(INFO) << "raw vector init success! name=" << meta_info_->Name()
            << ", multi_vids=" << multi_vids
            << ", vector_byte_size=" << vector_byte_size_
            << ", dimension=" << meta_info_->Dimension();
  return 0;
}

int RawVector::GetVector(long vid, ScopeVector &vec) const {
  return GetVector(vid, vec.ptr_, vec.deletable_);
}

int RawVector::Gets(const std::vector<int64_t> &vids,
                    ScopeVectors &vecs) const {
  bool deletable;
  for (size_t i = 0; i < vids.size(); i++) {
    const uint8_t *vec = nullptr;
    deletable = false;
    GetVector(vids[i], vec, deletable);
    vecs.Add(vec, deletable);
  }
  return 0;
}

int RawVector::Add(int docid, struct Field &field) {
  if (field.value.size() != (size_t)data_size_ * meta_info_->Dimension()) {
    LOG(ERROR) << "Doc [" << docid << "] len [" << field.value.size() << "]";
    return -1;
  }
  int ret = AddToStore((uint8_t *)field.value.c_str(), field.value.size());
  if (ret) {
    LOG(ERROR) << "add to store error, docid=" << docid << ", ret=" << ret;
    return -2;
  }

  return vid_mgr_->Add(meta_info_->size_++, docid);
}

int RawVector::Add(int docid, float *data) {
  int ret = AddToStore((uint8_t *)data, data_size_ * meta_info_->Dimension());
  if (ret) {
    LOG(ERROR) << "add to store error, docid=" << docid << ", ret=" << ret;
    return -2;
  }
  return vid_mgr_->Add(meta_info_->size_++, docid);
}

int RawVector::Update(int docid, struct Field &field) {
  if (vid_mgr_->MultiVids() || docid >= (int)meta_info_->Size()) {
    return -1;
  }

  int vid = docid;

  if (field.value.size() / data_size_ <= 0) {
    LOG(ERROR) << "Doc [" << docid << "] len " << field.value.size() << "]";
    return -1;
  }

  if (UpdateToStore(vid, (uint8_t *)field.value.c_str(), field.value.size())) {
    LOG(ERROR) << "update to store error, docid=" << docid;
    return -1;
  }

  // TODO: update source
  return 0;
}

DumpConfig *RawVector::GetDumpConfig() {
  return dynamic_cast<DumpConfig *>(&store_params_);
}

int StoreParams::Parse(const char *str) {
  utils::JsonParser jp;
  if (jp.Parse(str)) {
    LOG(ERROR) << "parse store parameters error: " << str;
    return -1;
  }
  return Parse(jp);
}

int StoreParams::Parse(utils::JsonParser &jp) {
  double cache_size = 0;
  if (!jp.GetDouble("cache_size", cache_size)) {
    if (cache_size > MAX_CACHE_SIZE || cache_size < 0) {
      LOG(ERROR) << "invalid cache size=" << cache_size << "M"
                 << ", limit size=" << MAX_CACHE_SIZE << "M";
      return -1;
    }
    this->cache_size = cache_size;
  }

  if (!jp.GetInt("segment_size", segment_size)) {
    if (segment_size <= 0) {
      LOG(ERROR) << "invalid segment size=" << segment_size;
      return -1;
    }
  }

  return 0;
}

int StoreParams::MergeRight(StoreParams &other) {
  cache_size = other.cache_size;
  segment_size = other.segment_size;
  return 0;
}

}  // namespace vearch

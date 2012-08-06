// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_shm.h"
#include "cras_types.h"
}

namespace {

class ShmTestSuite : public testing::Test{
  protected:
    virtual void SetUp() {
      memset(&shm_, 0, sizeof(shm_));
      shm_.area =
          static_cast<cras_audio_shm_area *>(calloc(1, sizeof(*shm_.area)));
      cras_shm_set_frame_bytes(&shm_, 4);
      cras_shm_set_used_size(&shm_, 1024);
      memcpy(&shm_.area->config, &shm_.config, sizeof(shm_.config));

      frames_ = 0;
    }

    virtual void TearDown() {
      free(shm_.area);
    }

    struct cras_audio_shm shm_;
    int16_t *buf_;
    size_t frames_;
};

// Test that and empty buffer returns 0 readable bytes.
TEST_F(ShmTestSuite, NoneReadableWhenEmpty) {
  buf_ = cras_shm_get_readable_frames(&shm_, 0, &frames_);
  EXPECT_EQ(0, frames_);
  cras_shm_buffer_read(&shm_, frames_);
  EXPECT_EQ(0, shm_.area->read_offset[0]);
}

// Buffer with 100 frames filled.
TEST_F(ShmTestSuite, OneHundredFilled) {
  shm_.area->write_offset[0] = 100 * shm_.area->config.frame_bytes;
  buf_ = cras_shm_get_readable_frames(&shm_, 0, &frames_);
  EXPECT_EQ(100, frames_);
  EXPECT_EQ((int16_t *)shm_.area->samples, buf_);
  cras_shm_buffer_read(&shm_, frames_ - 9);
  EXPECT_EQ((frames_ - 9) * shm_.config.frame_bytes, shm_.area->read_offset[0]);
  cras_shm_buffer_read(&shm_, 9);
  EXPECT_EQ(0, shm_.area->read_offset[0]);
  EXPECT_EQ(1, shm_.area->read_buf_idx);
}

// Buffer with 100 frames filled, 50 read.
TEST_F(ShmTestSuite, OneHundredFilled50Read) {
  shm_.area->write_offset[0] = 100 * shm_.config.frame_bytes;
  shm_.area->read_offset[0] = 50 * shm_.config.frame_bytes;
  buf_ = cras_shm_get_readable_frames(&shm_, 0, &frames_);
  EXPECT_EQ(50, frames_);
  EXPECT_EQ((int16_t *)(shm_.area->samples + shm_.area->read_offset[0]), buf_);
  cras_shm_buffer_read(&shm_, frames_ - 10);
  EXPECT_EQ(shm_.area->write_offset[0] - 10 * shm_.config.frame_bytes,
            shm_.area->read_offset[0]);
  cras_shm_buffer_read(&shm_, 10);
  EXPECT_EQ(0, shm_.area->read_offset[0]);
}

// Buffer with 100 frames filled, 50 read, offset by 25.
TEST_F(ShmTestSuite, OneHundredFilled50Read25offset) {
  shm_.area->write_offset[0] = 100 * shm_.config.frame_bytes;
  shm_.area->read_offset[0] = 50 * shm_.config.frame_bytes;
  buf_ = cras_shm_get_readable_frames(&shm_, 25, &frames_);
  EXPECT_EQ(25, frames_);
  EXPECT_EQ(shm_.area->samples + shm_.area->read_offset[0] +
                25 * shm_.area->config.frame_bytes,
            (uint8_t *)buf_);
}

// Test wrapping across buffers.
TEST_F(ShmTestSuite, WrapToNextBuffer) {
  shm_.config.used_size = 480 * shm_.config.frame_bytes;
  shm_.area->write_offset[0] = 240 * shm_.config.frame_bytes;
  shm_.area->read_offset[0] = 120 * shm_.config.frame_bytes;
  shm_.area->write_offset[1] = 240 * shm_.config.frame_bytes;
  buf_ = cras_shm_get_readable_frames(&shm_, 0, &frames_);
  EXPECT_EQ(120, frames_);
  EXPECT_EQ(shm_.area->samples + shm_.area->read_offset[0], (uint8_t *)buf_);
  buf_ = cras_shm_get_readable_frames(&shm_, frames_, &frames_);
  EXPECT_EQ(240, frames_);
  EXPECT_EQ(shm_.area->samples + shm_.config.used_size, (uint8_t *)buf_);
  cras_shm_buffer_read(&shm_, 350); /* Mark all-10 as read */
  EXPECT_EQ(0, shm_.area->read_offset[0]);
  EXPECT_EQ(230 * shm_.config.frame_bytes, shm_.area->read_offset[1]);
}

// Test wrapping last buffer.
TEST_F(ShmTestSuite, WrapFromFinalBuffer) {
  shm_.area->read_buf_idx = CRAS_NUM_SHM_BUFFERS - 1;
  shm_.config.used_size = 480 * shm_.config.frame_bytes;
  shm_.area->write_offset[shm_.area->read_buf_idx] =
      240 * shm_.config.frame_bytes;
  shm_.area->read_offset[shm_.area->read_buf_idx] =
      120 * shm_.config.frame_bytes;
  shm_.area->write_offset[0] = 240 * shm_.config.frame_bytes;
  buf_ = cras_shm_get_readable_frames(&shm_, 0, &frames_);
  EXPECT_EQ(120, frames_);
  EXPECT_EQ((uint8_t *)buf_, shm_.area->samples +
      shm_.config.used_size * shm_.area->read_buf_idx +
      shm_.area->read_offset[shm_.area->read_buf_idx]);
  buf_ = cras_shm_get_readable_frames(&shm_, frames_, &frames_);
  EXPECT_EQ(240, frames_);
  EXPECT_EQ(shm_.area->samples, (uint8_t *)buf_);
  cras_shm_buffer_read(&shm_, 350); /* Mark all-10 as read */
  EXPECT_EQ(0, shm_.area->read_offset[1]);
  EXPECT_EQ(230 * shm_.config.frame_bytes, shm_.area->read_offset[0]);
}

// Test Check available to write returns 0 if not free buffer.
TEST_F(ShmTestSuite, WriteAvailNotFree) {
  size_t ret;
  shm_.area->write_buf_idx = 0;
  shm_.area->write_offset[0] = 100 * shm_.config.frame_bytes;
  shm_.area->read_offset[0] = 50 * shm_.config.frame_bytes;
  ret = cras_shm_get_num_writeable(&shm_);
  EXPECT_EQ(0, ret);
}

// Test Check available to write returns num_frames if free buffer.
TEST_F(ShmTestSuite, WriteAvailValid) {
  size_t ret;
  shm_.area->write_buf_idx = 0;
  shm_.config.used_size = 480 * shm_.config.frame_bytes;
  shm_.area->write_offset[0] = 0;
  shm_.area->read_offset[0] = 0;
  ret = cras_shm_get_num_writeable(&shm_);
  EXPECT_EQ(480, ret);
}

TEST_F(ShmTestSuite, SetVolume) {
  cras_shm_set_volume_scaler(&shm_, 1.0);
  EXPECT_EQ(shm_.area->volume_scaler, 1.0);
  cras_shm_set_volume_scaler(&shm_, 1.4);
  EXPECT_EQ(shm_.area->volume_scaler, 1.0);
  cras_shm_set_volume_scaler(&shm_, -0.5);
  EXPECT_EQ(shm_.area->volume_scaler, 0.0);
  cras_shm_set_volume_scaler(&shm_, 0.5);
  EXPECT_EQ(shm_.area->volume_scaler, 0.5);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

// Copyright (C) <2019> Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0

#include "SoftVideoCompositor.h"

#include "libyuv/convert.h"
#include "libyuv/scale.h"

#include <iostream>
#include <fstream>

#include <boost/make_shared.hpp>

using namespace webrtc;
using namespace owt_base;

namespace mcu {

DEFINE_LOGGER(AvatarManager, "mcu.media.SoftVideoCompositor.AvatarManager");

AvatarManager::AvatarManager(uint8_t size)
    : m_size(size)
{
}

AvatarManager::~AvatarManager()
{
}

bool AvatarManager::getImageSize(const std::string &url, uint32_t *pWidth, uint32_t *pHeight)
{
    uint32_t width, height;
    size_t begin, end;
    char *str_end = NULL;

    begin = url.find('.');
    if (begin == std::string::npos) {
        ELOG_WARN("Invalid image size in url(%s)", url.c_str());
        return false;
    }

    end = url.find('x', begin);
    if (end == std::string::npos) {
        ELOG_WARN("Invalid image size in url(%s)", url.c_str());
        return false;
    }

    width = strtol(url.data() + begin + 1, &str_end, 10);
    if (url.data() + end != str_end) {
        ELOG_WARN("Invalid image size in url(%s)", url.c_str());
        return false;
    }

    begin = end;
    end = url.find('.', begin);
    if (end == std::string::npos) {
        ELOG_WARN("Invalid image size in url(%s)", url.c_str());
        return false;
    }

    height = strtol(url.data() + begin + 1, &str_end, 10);
    if (url.data() + end != str_end) {
        ELOG_WARN("Invalid image size in url(%s)", url.c_str());
        return false;
    }

    *pWidth = width;
    *pHeight = height;

    ELOG_TRACE("Image size in url(%s), %dx%d", url.c_str(), *pWidth, *pHeight);
    return true;
}

boost::shared_ptr<webrtc::VideoFrame> AvatarManager::loadImage(const std::string &url)
{
    uint32_t width, height;

    if (!getImageSize(url, &width, &height))
        return NULL;

    std::ifstream in(url, std::ios::in | std::ios::binary);

    in.seekg (0, in.end);
    uint32_t size = in.tellg();
    in.seekg (0, in.beg);

    if (size <= 0 || ((width * height * 3 + 1) / 2) != size) {
        ELOG_WARN("Open avatar image(%s) error, invalid size %d, expected size %d"
                , url.c_str(), size, (width * height * 3 + 1) / 2);
        return NULL;
    }

    char *image = new char [size];;
    in.read (image, size);
    in.close();

    rtc::scoped_refptr<I420Buffer> i420Buffer = I420Buffer::Copy(
            width, height,
            reinterpret_cast<const uint8_t *>(image), width,
            reinterpret_cast<const uint8_t *>(image + width * height), width / 2,
            reinterpret_cast<const uint8_t *>(image + width * height * 5 / 4), width / 2
            );

    boost::shared_ptr<webrtc::VideoFrame> frame(new webrtc::VideoFrame(i420Buffer, webrtc::kVideoRotation_0, 0));

    delete [] image;

    return frame;
}

bool AvatarManager::setAvatar(uint8_t index, const std::string &url)
{
    boost::unique_lock<boost::shared_mutex> lock(m_mutex);
    ELOG_DEBUG("setAvatar(%d) = %s", index, url.c_str());

    auto it = m_inputs.find(index);
    if (it == m_inputs.end()) {
        m_inputs[index] = url;
        return true;
    }

    if (it->second == url) {
        return true;
    }
    std::string old_url = it->second;
    it->second = url;

    //delete
    for (auto& it2 : m_inputs) {
        if (old_url == it2.second)
            return true;
    }
    m_frames.erase(old_url);
    return true;
}

bool AvatarManager::unsetAvatar(uint8_t index)
{
    boost::unique_lock<boost::shared_mutex> lock(m_mutex);
    ELOG_DEBUG("unsetAvatar(%d)", index);

    auto it = m_inputs.find(index);
    if (it == m_inputs.end()) {
        return true;
    }
    std::string url = it->second;
    m_inputs.erase(it);

    //delete
    for (auto& it2 : m_inputs) {
        if (url == it2.second)
            return true;
    }
    m_frames.erase(url);
    return true;
}

boost::shared_ptr<webrtc::VideoFrame> AvatarManager::getAvatarFrame(uint8_t index)
{
    boost::unique_lock<boost::shared_mutex> lock(m_mutex);

    auto it = m_inputs.find(index);
    if (it == m_inputs.end()) {
        ELOG_WARN("Not valid index(%d)", index);
        return NULL;
    }
    auto it2 = m_frames.find(it->second);
    if (it2 != m_frames.end()) {
        return it2->second;
    }

    boost::shared_ptr<webrtc::VideoFrame> frame = loadImage(it->second);
    m_frames[it->second] = frame;
    return frame;
}

DEFINE_LOGGER(SoftInput, "mcu.media.SoftVideoCompositor.SoftInput");

SoftInput::SoftInput()
    : m_active(false)
    , m_sync_enabled(true)
    , m_frame_sync_enabled(false)
{
    m_bufferManager.reset(new I420BufferManager(kMaxQueueSize));
    m_converter.reset(new owt_base::FrameConverter());
}

SoftInput::~SoftInput()
{
}

void SoftInput::setActive(bool active)
{
    boost::unique_lock<boost::shared_mutex> lock(m_mutex);
    m_active = active;
    if (!m_active)
        m_frame_queue.clear();
}

bool SoftInput::isActive(void)
{
    return m_active;
}

void SoftInput::pushInput(const owt_base::Frame& frame)
{
    assert(frame.format == owt_base::FRAME_FORMAT_I420);
    webrtc::VideoFrame* videoFrame = reinterpret_cast<webrtc::VideoFrame*>(frame.payload);

    {
        boost::unique_lock<boost::shared_mutex> lock(m_mutex);
        if (!m_active)
            return;

        if (m_frame_queue.size() == kMaxQueueSize) {
            ELOG_WARN("Input frame queue is full (%d), disable sync", kMaxQueueSize);

            // if the queue is full, it means the input is out of sync too much.
            // we have to disable sync, and not wait any more.
            m_frame_queue.clear();
            m_sync_enabled = false;
        }
    }

    rtc::scoped_refptr<webrtc::I420Buffer> dstBuffer = m_bufferManager->getFreeBuffer(videoFrame->width(), videoFrame->height());
    if (!dstBuffer) {
        ELOG_WARN("No free buffer");
        return;
    }

    rtc::scoped_refptr<webrtc::VideoFrameBuffer> srcI420Buffer = videoFrame->video_frame_buffer();
    if (!m_converter->convert(srcI420Buffer, dstBuffer.get())) {
        ELOG_ERROR("I420Copy failed");
        return;
    }

    {
        boost::unique_lock<boost::shared_mutex> lock(m_mutex);
        if (m_active) {
            std::shared_ptr<SoftInputFrame> inputFrame = std::make_shared<SoftInputFrame>();
            inputFrame->buffer = dstBuffer;
            inputFrame->timeStamp = frame.timeStamp;
            inputFrame->sync_enabled = frame.sync_enabled;
            inputFrame->sync_timeStamp = frame.sync_timeStamp ;

            m_frame_sync_enabled = frame.sync_enabled;
            if (!m_sync_enabled || !m_frame_sync_enabled)
                m_frame_queue.clear();

            m_frame_queue.push_back(inputFrame);
        }
    }
}

boost::shared_ptr<VideoFrame> SoftInput::popInput()
{
    boost::unique_lock<boost::shared_mutex> lock(m_mutex);

    if(!m_active)
        return NULL;

    if (m_frame_queue.empty())
        return nullptr;

    std::shared_ptr<SoftInputFrame> inputFrame = m_frame_queue.front();

    if (m_frame_queue.size() > 1)
        m_frame_queue.pop_front();

    return boost::make_shared<VideoFrame>(inputFrame->buffer, webrtc::kVideoRotation_0, 0);
}

std::shared_ptr<SoftInputFrame> SoftInput::front()
{
    boost::unique_lock<boost::shared_mutex> lock(m_mutex);

    if(!m_active)
        return nullptr;

    if (m_frame_queue.empty())
        return nullptr;

    return m_frame_queue.front();
}

std::shared_ptr<SoftInputFrame> SoftInput::back()
{
    boost::unique_lock<boost::shared_mutex> lock(m_mutex);

    if(!m_active)
        return nullptr;

    if (m_frame_queue.empty())
        return nullptr;

    return m_frame_queue.back();
}

std::shared_ptr<SoftInputFrame> SoftInput::get_sync_frame(int64_t sync_timeStamp)
{
    boost::unique_lock<boost::shared_mutex> lock(m_mutex);

    if(!m_active)
        return nullptr;

    if (m_frame_queue.empty())
        return nullptr;

    if (sync_timeStamp == -1)
        return m_frame_queue.front();

    while (true) {
        if (m_frame_queue.front()->sync_timeStamp >= sync_timeStamp)
            break;

        if (m_frame_queue.size() == 1)
            break;

        m_frame_queue.pop_front();
    }

    ELOG_DEBUG("Get sync frame %u", m_frame_queue.front()->sync_timeStamp);
    return m_frame_queue.front();
}

bool SoftInput::isSyncEnabled()
{
    boost::unique_lock<boost::shared_mutex> lock(m_mutex);
    return m_sync_enabled && m_frame_sync_enabled;
}

DEFINE_LOGGER(SoftFrameGenerator, "mcu.media.SoftVideoCompositor.SoftFrameGenerator");

SoftFrameGenerator::SoftFrameGenerator(
            SoftVideoCompositor *owner,
            owt_base::VideoSize &size,
            owt_base::YUVColor &bgColor,
            const bool crop,
            const uint32_t maxFps,
            const uint32_t minFps)
    : m_clock(Clock::GetRealTimeClock())
    , m_owner(owner)
    , m_maxSupportedFps(maxFps)
    , m_minSupportedFps(minFps)
    , m_counter(0)
    , m_counterMax(0)
    , m_size(size)
    , m_bgColor(bgColor)
    , m_crop(crop)
    , m_configureChanged(false)
    , m_parallelNum(0)
{
    ELOG_DEBUG_T("Support fps max(%d), min(%d)", m_maxSupportedFps, m_minSupportedFps);

    uint32_t fps = m_minSupportedFps;
    while (fps <= m_maxSupportedFps) {
        if (fps == m_maxSupportedFps)
            break;

        fps *= 2;
    }
    if (fps > m_maxSupportedFps) {
        ELOG_WARN_T("Invalid fps min(%d), max(%d) --> mix(%d), max(%d)"
                , m_minSupportedFps, m_maxSupportedFps
                , m_minSupportedFps, m_minSupportedFps
                );
        m_maxSupportedFps = m_minSupportedFps;
    }

    m_counter = 0;
    m_counterMax = m_maxSupportedFps / m_minSupportedFps;

    m_outputs.resize(m_maxSupportedFps / m_minSupportedFps);

    m_bufferManager.reset(new I420BufferManager(30));

#if 0 //disable parallet composition in sync mode
    // parallet composition
    uint32_t nThreads = boost::thread::hardware_concurrency();
    m_parallelNum = nThreads / 2;
    if (m_parallelNum > 16)
        m_parallelNum = 16;

    ELOG_DEBUG_T("hardware concurrency %d, parallel composition num %d", nThreads, m_parallelNum);

    if (m_parallelNum > 1) {
        m_srv       = boost::make_shared<boost::asio::io_service>();
        m_srvWork   = boost::make_shared<boost::asio::io_service::work>(*m_srv);
        m_thrGrp    = boost::make_shared<boost::thread_group>();

        for (uint32_t i = 0; i < m_parallelNum; i++)
            m_thrGrp->create_thread(boost::bind(&boost::asio::io_service::run, m_srv));
    }
#endif

    m_textDrawer.reset(new owt_base::FFmpegDrawText());

    m_jobTimer.reset(new JobTimer(m_maxSupportedFps, this));
    m_jobTimer->start();
}

SoftFrameGenerator::~SoftFrameGenerator()
{
    ELOG_DEBUG_T("Exit");

    if (m_srvWork)
        m_srvWork.reset();

    if (m_srv) {
        m_srv->stop();
        m_srv.reset();
    }

    if (m_thrGrp) {
        m_thrGrp->join_all();
        m_thrGrp.reset();
    }

    m_jobTimer->stop();

    for (uint32_t i = 0; i <  m_outputs.size(); i++) {
        if (m_outputs[i].size())
            ELOG_WARN_T("Outputs not empty!!!");
    }
}

void SoftFrameGenerator::updateLayoutSolution(LayoutSolution& solution)
{
    boost::unique_lock<boost::shared_mutex> lock(m_configMutex);

    m_newLayout         = solution;
    m_configureChanged  = true;
}

bool SoftFrameGenerator::isSupported(uint32_t width, uint32_t height, uint32_t fps)
{
    if (fps > m_maxSupportedFps || fps < m_minSupportedFps)
        return false;

    uint32_t n = m_minSupportedFps;
    while (n <= m_maxSupportedFps) {
        if (n == fps)
            return true;

        n *= 2;
    }

    return false;
}

bool SoftFrameGenerator::addOutput(const uint32_t width, const uint32_t height, const uint32_t fps, owt_base::FrameDestination *dst) {
    assert(isSupported(width, height, fps));

    boost::unique_lock<boost::shared_mutex> lock(m_outputMutex);

    int index = m_maxSupportedFps / fps - 1;

    Output_t output{.width = width, .height = height, .fps = fps, .dest = dst};
    m_outputs[index].push_back(output);
    return true;
}

bool SoftFrameGenerator::removeOutput(owt_base::FrameDestination *dst) {
    boost::unique_lock<boost::shared_mutex> lock(m_outputMutex);

    for (uint32_t i = 0; i < m_outputs.size(); i++) {
        for (auto it = m_outputs[i].begin(); it != m_outputs[i].end(); ++it) {
            if (it->dest == dst) {
                m_outputs[i].erase(it);
                return true;
            }
        }
    }

    return false;
}

void SoftFrameGenerator::onTimeout()
{
    bool hasValidOutput = false;
    {
        boost::unique_lock<boost::shared_mutex> lock(m_outputMutex);
        for (uint32_t i = 0; i < m_outputs.size(); i++) {
            if (m_counter % (i + 1))
                continue;

            if (m_outputs[i].size() > 0) {
                hasValidOutput = true;
                break;
            }
        }
    }

    if (hasValidOutput) {
        boost::shared_ptr<webrtc::VideoFrame> compositeFrame = generateFrame();
        if (compositeFrame) {
            owt_base::Frame frame;
            memset(&frame, 0, sizeof(frame));
            frame.format = owt_base::FRAME_FORMAT_I420;
            frame.payload = reinterpret_cast<uint8_t*>(compositeFrame.get());
            frame.length = 0; // unused.
            frame.timeStamp = compositeFrame->timestamp();
            frame.orig_timeStamp = frame.timeStamp;
            frame.additionalInfo.video.width = compositeFrame->width();
            frame.additionalInfo.video.height = compositeFrame->height();

            m_textDrawer->drawFrame(frame);

            {
                boost::unique_lock<boost::shared_mutex> lock(m_outputMutex);
                for (uint32_t i = 0; i <  m_outputs.size(); i++) {
                    if (m_counter % (i + 1))
                        continue;

                    for (auto it = m_outputs[i].begin(); it != m_outputs[i].end(); ++it) {
                        ELOG_TRACE_T("+++deliverFrame(%d), dst(%p), fps(%d), timestamp_ms(%u), timestamp(%u)"
                                , m_counter, it->dest, m_maxSupportedFps / (i + 1), frame.timeStamp / 90, frame.timeStamp);

                        it->dest->onFrame(frame);
                    }
                }
            }
        }
    }

    m_counter = (m_counter + 1) % m_counterMax;
}

boost::shared_ptr<webrtc::VideoFrame> SoftFrameGenerator::generateFrame()
{
    reconfigureIfNeeded();
    return layout();
}

void SoftFrameGenerator::layout_regions(SoftFrameGenerator *t, rtc::scoped_refptr<webrtc::I420Buffer> compositeBuffer, const LayoutSolution &regions)
{
    uint32_t composite_width = compositeBuffer->width();
    uint32_t composite_height = compositeBuffer->height();

    // sync to latest frame
    // example:
    //   frame-queue-1 [t3 t2 t1 t0]
    //   frame-queue-2 [t4 t3 t2 t1]
    //   frame-queue-3 [t5 t4 t3 t2]
    //   common sync timestamp range is [t3 t2], and we will sync to t3.
    int64_t min_sync_timeStamp = -1;
    int64_t max_sync_timeStamp = -1;
    for (LayoutSolution::const_iterator it = regions.begin(); it != regions.end(); ++it) {
        boost::shared_ptr<SoftInput> input = t->m_owner->getInput(it->input);
        if (!input->isSyncEnabled()) {
            continue;
        }

        std::shared_ptr<SoftInputFrame> front_frame = input->front();
        std::shared_ptr<SoftInputFrame> back_frame = input->back();
        if (front_frame == nullptr || back_frame == nullptr) {
            continue;
        }

        if (min_sync_timeStamp == -1 || min_sync_timeStamp < front_frame->sync_timeStamp)
            min_sync_timeStamp = front_frame->sync_timeStamp;

        if (max_sync_timeStamp == -1 || max_sync_timeStamp > back_frame->sync_timeStamp)
            max_sync_timeStamp = back_frame->sync_timeStamp;
    }
    ELOG_DEBUG("min_sync_timeStamp %ld, max_sync_timeStamp %ld", min_sync_timeStamp, max_sync_timeStamp);

    // min_sync_timeStamp == max_sync_timeStamp == -1, no sync inputs or sync inputs disabled. Fallback to non sync mode
    // min_sync_timeStamp > max_sync_timeStamp, valid sync inputs, but no sync frame. Keep front frame and wait for next sync
    // min_sync_timeStamp <= max_sync_timeStamp, valid sync frame, use max_sync_timeStamp
    for (LayoutSolution::const_iterator it = regions.begin(); it != regions.end(); ++it) {
        boost::shared_ptr<webrtc::VideoFrame> inputFrame;

        if (max_sync_timeStamp == -1) {
            inputFrame = t->m_owner->getInputFrame(it->input);
        } else if (min_sync_timeStamp > max_sync_timeStamp) {
            inputFrame = t->m_owner->getSyncInputFrame(it->input, -1);
        } else {
            inputFrame = t->m_owner->getSyncInputFrame(it->input, max_sync_timeStamp);
        }

        if (inputFrame == NULL) {
            continue;
        }

        rtc::scoped_refptr<webrtc::VideoFrameBuffer> inputBuffer = inputFrame->video_frame_buffer();

        Region region = it->region;
        uint32_t dst_x      = (uint64_t)composite_width * region.area.rect.left.numerator / region.area.rect.left.denominator;
        uint32_t dst_y      = (uint64_t)composite_height * region.area.rect.top.numerator / region.area.rect.top.denominator;
        uint32_t dst_width  = (uint64_t)composite_width * region.area.rect.width.numerator / region.area.rect.width.denominator;
        uint32_t dst_height = (uint64_t)composite_height * region.area.rect.height.numerator / region.area.rect.height.denominator;

        if (dst_x + dst_width > composite_width)
            dst_width = composite_width - dst_x;

        if (dst_y + dst_height > composite_height)
            dst_height = composite_height - dst_y;

        uint32_t cropped_dst_width;
        uint32_t cropped_dst_height;
        uint32_t src_x;
        uint32_t src_y;
        uint32_t src_width;
        uint32_t src_height;
        if (t->m_crop) {
            src_width   = std::min((uint32_t)inputBuffer->width(), dst_width * inputBuffer->height() / dst_height);
            src_height  = std::min((uint32_t)inputBuffer->height(), dst_height * inputBuffer->width() / dst_width);
            src_x       = (inputBuffer->width() - src_width) / 2;
            src_y       = (inputBuffer->height() - src_height) / 2;

            cropped_dst_width   = dst_width;
            cropped_dst_height  = dst_height;
        } else {
            src_width   = inputBuffer->width();
            src_height  = inputBuffer->height();
            src_x       = 0;
            src_y       = 0;

            cropped_dst_width   = std::min(dst_width, inputBuffer->width() * dst_height / inputBuffer->height());
            cropped_dst_height  = std::min(dst_height, inputBuffer->height() * dst_width / inputBuffer->width());
        }

        dst_x += (dst_width - cropped_dst_width) / 2;
        dst_y += (dst_height - cropped_dst_height) / 2;

        src_x               &= ~1;
        src_y               &= ~1;
        src_width           &= ~1;
        src_height          &= ~1;
        dst_x               &= ~1;
        dst_y               &= ~1;
        cropped_dst_width   &= ~1;
        cropped_dst_height  &= ~1;

        int ret = libyuv::I420Scale(
                inputBuffer->DataY() + src_y * inputBuffer->StrideY() + src_x, inputBuffer->StrideY(),
                inputBuffer->DataU() + (src_y * inputBuffer->StrideU() + src_x) / 2, inputBuffer->StrideU(),
                inputBuffer->DataV() + (src_y * inputBuffer->StrideV() + src_x) / 2, inputBuffer->StrideV(),
                src_width, src_height,
                compositeBuffer->MutableDataY() + dst_y * compositeBuffer->StrideY() + dst_x, compositeBuffer->StrideY(),
                compositeBuffer->MutableDataU() + (dst_y * compositeBuffer->StrideU() + dst_x) / 2, compositeBuffer->StrideU(),
                compositeBuffer->MutableDataV() + (dst_y * compositeBuffer->StrideV() + dst_x) / 2, compositeBuffer->StrideV(),
                cropped_dst_width, cropped_dst_height,
                libyuv::kFilterBox);
        if (ret != 0)
            ELOG_ERROR("I420Scale failed, ret %d", ret);
    }
}

boost::shared_ptr<webrtc::VideoFrame> SoftFrameGenerator::layout()
{
    rtc::scoped_refptr<webrtc::I420Buffer> compositeBuffer = m_bufferManager->getFreeBuffer(m_size.width, m_size.height);
    if (!compositeBuffer) {
        ELOG_ERROR("No valid composite buffer");
        return NULL;
    }

#if 0
    if (m_layout.size() == 1) {
        boost::shared_ptr<webrtc::VideoFrame> inputFrame = m_owner->getInputFrame(m_layout.front().input);
        return inputFrame;
    }
#endif

    // Set the background color
    libyuv::I420Rect(
            compositeBuffer->MutableDataY(), compositeBuffer->StrideY(),
            compositeBuffer->MutableDataU(), compositeBuffer->StrideU(),
            compositeBuffer->MutableDataV(), compositeBuffer->StrideV(),
            0, 0, compositeBuffer->width(), compositeBuffer->height(),
            m_bgColor.y, m_bgColor.cb, m_bgColor.cr);

    bool isParallelFrameComposition = m_parallelNum > 1 && m_layout.size() > 4;

    if (isParallelFrameComposition) {
        int nParallelRegions = (m_layout.size() + m_parallelNum - 1) / m_parallelNum;
        int nRegions = m_layout.size();

        LayoutSolution::iterator regions_begin = m_layout.begin();
        LayoutSolution::iterator regions_end = m_layout.begin();

        std::vector<boost::shared_ptr<boost::packaged_task<void>>> tasks;
        while (nRegions > 0) {
            if (nRegions < nParallelRegions)
                nParallelRegions = nRegions;

            regions_begin = regions_end;
            advance(regions_end, nParallelRegions);

            boost::shared_ptr<boost::packaged_task<void>> task = boost::make_shared<boost::packaged_task<void>>(
                    boost::bind(SoftFrameGenerator::layout_regions,
                        this,
                        compositeBuffer,
                        LayoutSolution(regions_begin, regions_end))
                    );
            m_srv->post(boost::bind(&boost::packaged_task<void>::operator(), task));
            tasks.push_back(task);

            nRegions -= nParallelRegions;
        }

        for (auto& task : tasks)
            task->get_future().wait();
    } else {
        layout_regions(this, compositeBuffer, m_layout);
    }

    int64_t timestamp_ms = m_clock->TimeInMilliseconds();

    boost::shared_ptr<webrtc::VideoFrame> video_frame =
        boost::make_shared<webrtc::VideoFrame>(
                compositeBuffer,
                timestamp_ms * 1000 / 90,
                timestamp_ms,
                webrtc::kVideoRotation_0
                );
    video_frame->set_ntp_time_ms(timestamp_ms);

    return video_frame;
}

void SoftFrameGenerator::reconfigureIfNeeded()
{
    {
        boost::unique_lock<boost::shared_mutex> lock(m_configMutex);
        if (!m_configureChanged)
            return;

        m_layout = m_newLayout;
        m_configureChanged = false;
    }

    ELOG_DEBUG_T("reconfigure");
}

void SoftFrameGenerator::drawText(const std::string& textSpec)
{
    m_textDrawer->setText(textSpec);
    m_textDrawer->enable(true);
}

void SoftFrameGenerator::clearText()
{
    m_textDrawer->enable(false);
}

DEFINE_LOGGER(SoftVideoCompositor, "mcu.media.SoftVideoCompositor");

SoftVideoCompositor::SoftVideoCompositor(uint32_t maxInput, VideoSize rootSize, YUVColor bgColor, bool crop)
    : m_maxInput(maxInput)
{
    m_inputs.resize(m_maxInput);
    for (auto& input : m_inputs) {
        input.reset(new SoftInput());
    }

    m_avatarManager.reset(new AvatarManager(maxInput));

    m_generators.resize(2);
    m_generators[0].reset(new SoftFrameGenerator(this, rootSize, bgColor, crop, 60, 15));
    m_generators[1].reset(new SoftFrameGenerator(this, rootSize, bgColor, crop, 48, 6));
}

SoftVideoCompositor::~SoftVideoCompositor()
{
    m_generators.clear();
    m_avatarManager.reset();
    m_inputs.clear();
}

void SoftVideoCompositor::updateRootSize(VideoSize& rootSize)
{
    ELOG_WARN("Not support updateRootSize: %dx%d", rootSize.width, rootSize.height);
}

void SoftVideoCompositor::updateBackgroundColor(YUVColor& bgColor)
{
    ELOG_WARN("Not support updateBackgroundColor: YCbCr(0x%x, 0x%x, 0x%x)", bgColor.y, bgColor.cb, bgColor.cr);
}

void SoftVideoCompositor::updateLayoutSolution(LayoutSolution& solution)
{
    assert(solution.size() <= m_maxInput);

    for (auto& generator : m_generators) {
        generator->updateLayoutSolution(solution);
    }
}

bool SoftVideoCompositor::activateInput(int input)
{
    m_inputs[input]->setActive(true);
    return true;
}

void SoftVideoCompositor::deActivateInput(int input)
{
    m_inputs[input]->setActive(false);
}

bool SoftVideoCompositor::setAvatar(int input, const std::string& avatar)
{
    return m_avatarManager->setAvatar(input, avatar);
}

bool SoftVideoCompositor::unsetAvatar(int input)
{
    return m_avatarManager->unsetAvatar(input);
}

void SoftVideoCompositor::pushInput(int input, const Frame& frame)
{
    m_inputs[input]->pushInput(frame);
}

bool SoftVideoCompositor::addOutput(const uint32_t width, const uint32_t height, const uint32_t framerateFPS, owt_base::FrameDestination *dst)
{
    ELOG_DEBUG("addOutput, %dx%d, fps(%d), dst(%p)", width, height, framerateFPS, dst);

    for (auto& generator : m_generators) {
        if (generator->isSupported(width, height, framerateFPS)) {
            return generator->addOutput(width, height, framerateFPS, dst);
        }
    }

    ELOG_ERROR("Can not addOutput, %dx%d, fps(%d), dst(%p)", width, height, framerateFPS, dst);
    return false;
}

bool SoftVideoCompositor::removeOutput(owt_base::FrameDestination *dst)
{
    ELOG_DEBUG("removeOutput, dst(%p)", dst);

    for (auto& generator : m_generators) {
        if (generator->removeOutput(dst)) {
            return true;
        }
    }

    ELOG_ERROR("Can not removeOutput, dst(%p)", dst);
    return false;
}

boost::shared_ptr<webrtc::VideoFrame> SoftVideoCompositor::getInputFrame(int index)
{
    boost::shared_ptr<webrtc::VideoFrame> src;

    auto& input = m_inputs[index];
    if (input->isActive()) {
        src = input->popInput();
    } else {
        src = m_avatarManager->getAvatarFrame(index);
    }

    return src;
}

boost::shared_ptr<webrtc::VideoFrame> SoftVideoCompositor::getSyncInputFrame(int index, int64_t sync_timeStamp)
{
    auto& input = m_inputs[index];
    if (!input->isActive())
        return m_avatarManager->getAvatarFrame(index);

    if (!input->isSyncEnabled())
        return input->popInput();

    std::shared_ptr<SoftInputFrame> input_frame = input->get_sync_frame(sync_timeStamp);
    if (input_frame)
        return boost::make_shared<VideoFrame>(input_frame->buffer, webrtc::kVideoRotation_0, 0);
    else
        return nullptr;
}

boost::shared_ptr<SoftInput> SoftVideoCompositor::getInput(int index)
{
    return m_inputs[index];
}

void SoftVideoCompositor::drawText(const std::string& textSpec)
{
    for (auto& generator : m_generators) {
        generator->drawText(textSpec);
    }
}

void SoftVideoCompositor::clearText()
{
    for (auto& generator : m_generators) {
        generator->clearText();
    }
}

}

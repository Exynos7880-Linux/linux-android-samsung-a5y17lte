/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is video functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can revratribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <video/videonode.h>
#include <media/exynos_mc.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/videodev2.h>
#include <linux/videodev2_exynos_media.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/v4l2-mediabus.h>
#include <linux/bug.h>

#include "fimc-is-core.h"
#include "fimc-is-cmd.h"
#include "fimc-is-regs.h"
#include "fimc-is-err.h"
#include "fimc-is-video.h"
#include "fimc-is-metadata.h"

const struct v4l2_file_operations fimc_is_vra_video_fops;
const struct v4l2_ioctl_ops fimc_is_vra_video_ioctl_ops;
const struct vb2_ops fimc_is_vra_qops;

int fimc_is_vra_video_probe(void *data)
{
	int ret = 0;
	struct fimc_is_core *core;
	struct fimc_is_video *video;

	BUG_ON(!data);

	core = (struct fimc_is_core *)data;
	video = &core->video_vra;
	video->resourcemgr = &core->resourcemgr;

	if (!core->pdev) {
		probe_err("pdev is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_video_probe(video,
		FIMC_IS_VIDEO_VRA_NAME,
		FIMC_IS_VIDEO_VRA_NUM,
		VFL_DIR_M2M,
		&core->resourcemgr.mem,
		&core->v4l2_dev,
		&fimc_is_vra_video_fops,
		&fimc_is_vra_video_ioctl_ops);
	if (ret)
		dev_err(&core->pdev->dev, "%s is fail(%d)\n", __func__, ret);

p_err:
	return ret;
}

static int fimc_is_vra_video_open(struct file *file)
{
	int ret = 0;
	struct fimc_is_video *video;
	struct fimc_is_video_ctx *vctx;
	struct fimc_is_device_ischain *device;
	struct fimc_is_resourcemgr *resourcemgr;

	vctx = NULL;
	device = NULL;
	video = video_drvdata(file);
	resourcemgr = video->resourcemgr;
	if (!resourcemgr) {
		err("resourcemgr is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	ret = fimc_is_resource_open(resourcemgr, RESOURCE_TYPE_ISCHAIN, (void **)&device);
	if (ret) {
		err("fimc_is_resource_open is fail(%d)", ret);
		goto p_err;
	}

	if (!device) {
		err("device is NULL");
		ret = -EINVAL;
		goto p_err;
	}

	minfo("[VRA:V] %s\n", device, __func__);

	ret = open_vctx(file, video, &vctx, device->instance, FRAMEMGR_ID_VRA);
	if (ret) {
		merr("open_vctx is fail(%d)", device, ret);
		goto p_err;
	}

	ret = fimc_is_video_open(vctx,
		device,
		VIDEO_VRA_READY_BUFFERS,
		video,
		&fimc_is_vra_qops,
		&fimc_is_ischain_vra_ops);
	if (ret) {
		merr("fimc_is_video_open is fail(%d)", device, ret);
		close_vctx(file, video, vctx);
		goto p_err;
	}

	ret = fimc_is_ischain_vra_open(device, vctx);
	if (ret) {
		merr("fimc_is_ischain_vra_open is fail(%d)", device, ret);
		close_vctx(file, video, vctx);
		goto p_err;
	}

p_err:
	return ret;
}

static int fimc_is_vra_video_close(struct file *file)
{
	int ret = 0;
	int refcount;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_video *video;
	struct fimc_is_device_ischain *device;

	BUG_ON(!file);
	BUG_ON(!vctx);
	BUG_ON(!GET_VIDEO(vctx));
	BUG_ON(!GET_DEVICE(vctx));

	video = GET_VIDEO(vctx);
	device = GET_DEVICE(vctx);

	ret = fimc_is_ischain_vra_close(device, vctx);
	if (ret)
		merr("fimc_is_ischain_vra_close is fail(%d)", device, ret);

	ret = fimc_is_video_close(vctx);
	if (ret)
		merr("fimc_is_video_close is fail(%d)", device, ret);

	refcount = close_vctx(file, video, vctx);
	if (refcount < 0)
		merr("close_vctx is fail(%d)", device, refcount);

	minfo("[VRA:V] %s(%d,%d):%d\n", device, __func__, atomic_read(&device->open_cnt), refcount, ret);

	return ret;
}

static unsigned int fimc_is_vra_video_poll(struct file *file,
	struct poll_table_struct *wait)
{
	u32 ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	ret = fimc_is_video_poll(file, vctx, wait);
	if (ret)
		merr("fimc_is_video_poll is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_vra_video_mmap(struct file *file,
	struct vm_area_struct *vma)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	ret = fimc_is_video_mmap(file, vctx, vma);
	if (ret)
		merr("fimc_is_video_mmap is fail(%d)", vctx, ret);

	return ret;
}

const struct v4l2_file_operations fimc_is_vra_video_fops = {
	.owner		= THIS_MODULE,
	.open		= fimc_is_vra_video_open,
	.release	= fimc_is_vra_video_close,
	.poll		= fimc_is_vra_video_poll,
	.unlocked_ioctl	= video_ioctl2,
	.mmap		= fimc_is_vra_video_mmap,
};

static int fimc_is_vra_video_querycap(struct file *file, void *fh,
					struct v4l2_capability *cap)
{
	struct fimc_is_video *video = video_drvdata(file);

	BUG_ON(!cap);
	BUG_ON(!video);

	snprintf(cap->driver, sizeof(cap->driver), "%s", video->vd.name);
	snprintf(cap->card, sizeof(cap->card), "%s", video->vd.name);
	cap->capabilities |= V4L2_CAP_STREAMING
			| V4L2_CAP_VIDEO_OUTPUT
			| V4L2_CAP_VIDEO_OUTPUT_MPLANE;
	cap->device_caps |= cap->capabilities;

	return 0;
}

static int fimc_is_vra_video_enum_fmt_mplane(struct file *file, void *priv,
	struct v4l2_fmtdesc *f)
{
	/* Todo: add enum format control code */
	return 0;
}

static int fimc_is_vra_video_get_format_mplane(struct file *file, void *fh,
	struct v4l2_format *format)
{
	/* Todo: add get format control code */
	return 0;
}

static int fimc_is_vra_video_set_format_mplane(struct file *file, void *fh,
	struct v4l2_format *format)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	BUG_ON(!vctx);
	BUG_ON(!format);

	mdbgv_vra("%s\n", vctx, __func__);

	ret = fimc_is_video_set_format_mplane(file, vctx, format);
	if (ret) {
		merr("fimc_is_video_set_format_mplane is fail(%d)", vctx, ret);
		goto p_err;
	}

p_err:
	return ret;
}

static int fimc_is_vra_video_reqbufs(struct file *file, void *priv,
	struct v4l2_requestbuffers *buf)
{
	int ret;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_vra("%s(buffers : %d)\n", vctx, __func__, buf->count);

	ret = fimc_is_video_reqbufs(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_reqbufs is fail(error %d)", vctx, ret);

	return ret;
}

static int fimc_is_vra_video_querybuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_vra("%s\n", vctx, __func__);

	ret = fimc_is_video_querybuf(file, vctx, buf);
	if (ret)
		merr("fimc_is_video_querybuf is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_vra_video_qbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_queue *queue;

	BUG_ON(!vctx);

#ifdef DBG_STREAMING
	mdbgv_vra("%s\n", vctx, __func__);
#endif

	queue = GET_QUEUE(vctx);

	if (!test_bit(FIMC_IS_QUEUE_STREAM_ON, &queue->state)) {
		merr("stream off state, can NOT qbuf", vctx);
		ret = -EINVAL;
		goto p_err;
	}

	ret = CALL_VOPS(vctx, qbuf, buf);
	if (ret)
		merr("qbuf is fail(%d)", vctx, ret);

p_err:
	return ret;
}

static int fimc_is_vra_video_dqbuf(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;
	bool blocking = file->f_flags & O_NONBLOCK;

#ifdef DBG_STREAMING
	mdbgv_vra("%s\n", vctx, __func__);
#endif

	ret = CALL_VOPS(vctx, dqbuf, buf, blocking);
	if (ret)
		merr("dqbuf is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_vra_video_prepare(struct file *file, void *priv,
	struct v4l2_buffer *buf)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_device_ischain *device;
	struct fimc_is_framemgr *framemgr;
	struct fimc_is_frame *frame;

	BUG_ON(!buf);
	BUG_ON(!vctx);
	BUG_ON(!GET_FRAMEMGR(vctx));
	BUG_ON(!GET_DEVICE(vctx));

#ifdef DBG_STREAMING
	mdbgv_vra("%s\n", vctx, __func__);
#endif

	device = GET_DEVICE(vctx);
	framemgr = GET_FRAMEMGR(vctx);
	frame = &framemgr->frames[buf->index];

	ret = fimc_is_video_prepare(file, vctx, buf);
	if (ret) {
	       merr("fimc_is_video_prepare is fail(%d)", vctx, ret);
	       goto p_err;
	}

	if (!test_bit(FRAME_MEM_MAPPED, &frame->mem_state)) {
		fimc_is_itf_map(device, GROUP_ID(device->group_vra.id), frame->dvaddr_shot, frame->shot_size);
		set_bit(FRAME_MEM_MAPPED, &frame->mem_state);
	}

p_err:
	return ret;
}

static int fimc_is_vra_video_streamon(struct file *file, void *priv,
	enum v4l2_buf_type type)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_vra("%s\n", vctx, __func__);

	ret = fimc_is_video_streamon(file, vctx, type);
	if (ret)
		merr("fimc_is_vra_video_streamon is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_vra_video_streamoff(struct file *file, void *priv,
	enum v4l2_buf_type type)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;

	mdbgv_vra("%s\n", vctx, __func__);

	ret = fimc_is_video_streamoff(file, vctx, type);
	if (ret)
		merr("fimc_is_video_streamoff is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_vra_video_enum_input(struct file *file, void *priv,
						struct v4l2_input *input)
{
	/* Todo : add to enum input control code */
	return 0;
}

static int fimc_is_vra_video_g_input(struct file *file, void *priv,
	unsigned int *input)
{
	/* Todo: add get input control code */
	return 0;
}

static int fimc_is_vra_video_s_input(struct file *file, void *priv,
	unsigned int input)
{
	int ret = 0;
	u32 stream, module, vindex, intype, leader;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_device_ischain *device;

	BUG_ON(!vctx);
	BUG_ON(!vctx->device);

	device = GET_DEVICE(vctx);
	stream = (input & INPUT_STREAM_MASK) >> INPUT_STREAM_SHIFT;
	module = (input & INPUT_MODULE_MASK) >> INPUT_MODULE_SHIFT;
	vindex = (input & INPUT_VINDEX_MASK) >> INPUT_VINDEX_SHIFT;
	intype = (input & INPUT_INTYPE_MASK) >> INPUT_INTYPE_SHIFT;
	leader = (input & INPUT_LEADER_MASK) >> INPUT_LEADER_SHIFT;

	mdbgv_vra("%s(input : %08X)[%d,%d,%d,%d,%d]\n", vctx, __func__, input,
			stream, module, vindex, intype, leader);

	ret = fimc_is_video_s_input(file, vctx);
	if (ret) {
		merr("fimc_is_video_s_input is fail(%d)", vctx, ret);
		goto p_err;
	}

	ret = fimc_is_ischain_vra_s_input(device, stream, module, vindex, intype, leader);
	if (ret) {
		merr("fimc_is_ischain_isp_s_input is fail(%d)", vctx, ret);
		goto p_err;
	}

p_err:
	return ret;
}

static int fimc_is_vra_video_s_ctrl(struct file *file, void *priv,
	struct v4l2_control *ctrl)
{
int ret = 0;
	struct fimc_is_video_ctx *vctx = file->private_data;
	struct fimc_is_device_ischain *device;

	BUG_ON(!vctx);
	BUG_ON(!GET_DEVICE(vctx));
	BUG_ON(!ctrl);

	mdbgv_vra("%s\n", vctx, __func__);

	device = GET_DEVICE(vctx);

	switch (ctrl->id) {
	case V4L2_CID_IS_FORCE_DONE:
		set_bit(FIMC_IS_GROUP_REQUEST_FSTOP, &device->group_vra.state);
		break;
	default:
		ret = fimc_is_video_s_ctrl(file, vctx, ctrl);
		if (ret) {
			err("fimc_is_video_s_ctrl is fail(%d)", ret);
			goto p_err;
		}
		break;
	}

p_err:
	return ret;
}

static int fimc_is_vra_video_g_ctrl(struct file *file, void *priv,
	struct v4l2_control *ctrl)
{
	/* Todo: add get control code */
	return 0;
}

static int fimc_is_vra_video_g_ext_ctrl(struct file *file, void *priv,
	struct v4l2_ext_controls *ctrls)
{
	/* Todo: add get extra control code */
	return 0;
}

const struct v4l2_ioctl_ops fimc_is_vra_video_ioctl_ops = {
	.vidioc_querycap		= fimc_is_vra_video_querycap,
	.vidioc_enum_fmt_vid_out_mplane	= fimc_is_vra_video_enum_fmt_mplane,
	.vidioc_g_fmt_vid_out_mplane	= fimc_is_vra_video_get_format_mplane,
	.vidioc_s_fmt_vid_out_mplane	= fimc_is_vra_video_set_format_mplane,
	.vidioc_reqbufs			= fimc_is_vra_video_reqbufs,
	.vidioc_querybuf		= fimc_is_vra_video_querybuf,
	.vidioc_qbuf			= fimc_is_vra_video_qbuf,
	.vidioc_dqbuf			= fimc_is_vra_video_dqbuf,
	.vidioc_prepare_buf		= fimc_is_vra_video_prepare,
	.vidioc_streamon		= fimc_is_vra_video_streamon,
	.vidioc_streamoff		= fimc_is_vra_video_streamoff,
	.vidioc_enum_input		= fimc_is_vra_video_enum_input,
	.vidioc_g_input			= fimc_is_vra_video_g_input,
	.vidioc_s_input			= fimc_is_vra_video_s_input,
	.vidioc_s_ctrl			= fimc_is_vra_video_s_ctrl,
	.vidioc_g_ctrl			= fimc_is_vra_video_g_ctrl,
	.vidioc_g_ext_ctrls		= fimc_is_vra_video_g_ext_ctrl,
};

static int fimc_is_vra_queue_setup(struct vb2_queue *vbq,
	const struct v4l2_format *fmt,
	unsigned int *num_buffers, unsigned int *num_planes,
	unsigned int sizes[],
	void *allocators[])
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = vbq->drv_priv;
	struct fimc_is_video *video;
	struct fimc_is_queue *queue;

	BUG_ON(!vctx);
	BUG_ON(!vctx->video);

	mdbgv_isp("%s\n", vctx, __func__);

	video = GET_VIDEO(vctx);
	queue = GET_QUEUE(vctx);

	ret = fimc_is_queue_setup(queue,
		video->alloc_ctx,
		num_planes,
		sizes,
		allocators);
	if (ret)
		merr("fimc_is_queue_setup is fail(%d)", vctx, ret);

	return ret;
}

static int fimc_is_vra_buffer_prepare(struct vb2_buffer *vb)
{
	return fimc_is_queue_prepare(vb);
}

static inline void fimc_is_vra_wait_prepare(struct vb2_queue *vbq)
{
	fimc_is_queue_wait_prepare(vbq);
}

static inline void fimc_is_vra_wait_finish(struct vb2_queue *vbq)
{
	fimc_is_queue_wait_finish(vbq);
}

static int fimc_is_vra_start_streaming(struct vb2_queue *vbq,
	unsigned int count)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = vbq->drv_priv;
	struct fimc_is_queue *queue;
	struct fimc_is_device_ischain *device;

	BUG_ON(!vctx);
	BUG_ON(!GET_DEVICE(vctx));

	mdbgv_vra("%s\n", vctx, __func__);

	device = GET_DEVICE(vctx);
	queue = GET_QUEUE(vctx);

	ret = fimc_is_queue_start_streaming(queue, device);
	if (ret) {
		merr("fimc_is_queue_start_streaming is fail(%d)", device, ret);
		goto p_err;
	}

p_err:
	return ret;
}

static void fimc_is_vra_stop_streaming(struct vb2_queue *vbq)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = vbq->drv_priv;
	struct fimc_is_queue *queue;
	struct fimc_is_device_ischain *device;

	BUG_ON(!vctx);
	BUG_ON(!GET_DEVICE(vctx));

	mdbgv_vra("%s\n", vctx, __func__);

	device = GET_DEVICE(vctx);
	queue = GET_QUEUE(vctx);

	ret = fimc_is_queue_stop_streaming(queue, device);
	if (ret) {
		merr("fimc_is_queue_stop_streaming is fail(%d)", device, ret);
		return;
	}
}

static void fimc_is_vra_buffer_queue(struct vb2_buffer *vb)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = vb->vb2_queue->drv_priv;
	struct fimc_is_queue *queue;
	struct fimc_is_device_ischain *device;

	BUG_ON(!vctx);
	BUG_ON(!GET_DEVICE(vctx));

#ifdef DBG_STREAMING
	mdbgv_vra("%s(%d)\n", vctx, __func__, vb->v4l2_buf.index);
#endif

	device = GET_DEVICE(vctx);
	queue = GET_QUEUE(vctx);

	ret = fimc_is_queue_buffer_queue(queue, vb);
	if (ret) {
		merr("fimc_is_queue_buffer_queue is fail(%d)", device, ret);
		return;
	}

	ret = fimc_is_ischain_vra_buffer_queue(device, queue, vb->v4l2_buf.index);
	if (ret) {
		merr("fimc_is_ischain_vra_buffer_queue is fail(%d)", device, ret);
		return;
	}
}

static void fimc_is_vra_buffer_finish(struct vb2_buffer *vb)
{
	int ret = 0;
	struct fimc_is_video_ctx *vctx = vb->vb2_queue->drv_priv;
	struct fimc_is_device_ischain *device = vctx->device;

	BUG_ON(!vctx);
	BUG_ON(!GET_DEVICE(vctx));

#ifdef DBG_STREAMING
	mdbgv_vra("%s(%d)\n", vctx, __func__, vb->v4l2_buf.index);
#endif

	device = GET_DEVICE(vctx);

	ret = fimc_is_ischain_vra_buffer_finish(device, vb->v4l2_buf.index);
	if (ret) {
		merr("fimc_is_ischain_vra_buffer_finish is fail(%d)", device, ret);
		return;
	}
}

static int fimc_is_vra_buffer_init(struct vb2_buffer *vb)
{
	struct fimc_is_vb2_buf *vbuf = vb_to_fimc_is_vb2_buf(vb);
	struct fimc_is_video_ctx *vctx = vb->vb2_queue->drv_priv;
	unsigned int plane;

	vbuf->ops = vctx->fimc_is_vb2_buf_ops;

	for (plane = 0; plane < vb->num_planes; ++plane) {
		vbuf->kva[plane] = vbuf->ops->plane_kvaddr(vbuf, plane);
		vbuf->dva[plane] = vbuf->ops->plane_dvaddr(vbuf, plane);
	}

	return 0;
}

const struct vb2_ops fimc_is_vra_qops = {
	.queue_setup		= fimc_is_vra_queue_setup,
	.buf_init		= fimc_is_vra_buffer_init,
	.buf_prepare		= fimc_is_vra_buffer_prepare,
	.buf_queue		= fimc_is_vra_buffer_queue,
	.buf_finish		= fimc_is_vra_buffer_finish,
	.wait_prepare		= fimc_is_vra_wait_prepare,
	.wait_finish		= fimc_is_vra_wait_finish,
	.start_streaming	= fimc_is_vra_start_streaming,
	.stop_streaming		= fimc_is_vra_stop_streaming,
};


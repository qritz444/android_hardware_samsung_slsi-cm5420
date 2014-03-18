/*
**
** Copyright 2012, Samsung Electronics Co. LTD
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

/*!
 * \file      ExynosCameraHWInterface.h
 * \brief     source file for Android Camera HAL
 * \author    thun.hwang(thun.hwang@samsung.com)
 * \date      2010/06/03
 *
 * <b>Revision History: </b>
 * - 2011/12/31 : thun.hwang(thun.hwang@samsung.com) \n
 *   Initial version
 *
 * - 2012/02/01 : Sangwoo, Park(sw5771.park@samsung.com) \n
 *   Adjust Android Standard features
 *
 * - 2012/03/14 : sangwoo.park(sw5771.park@samsung.com) \n
 *   Change file, class name to ExynosXXX.
 *
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "ExynosCameraHWInterface"
#include <cutils/log.h>

//#include "ExynosCameraHWInterface.h"
#include "ExynosCameraHWImpl.h"

#include "exynos_format.h"

#include "SecCameraHardware.h"  //mhjang add

/*
#define CHECK_PREVIEW_TIME 0

#if ((CHECK_PREVIEW_TIME))
struct timeval oldtime, curtime;
#endif

#define VIDEO_COMMENT_MARKER_H          (0xFFBE)
#define VIDEO_COMMENT_MARKER_L          (0xFFBF)
#define VIDEO_COMMENT_MARKER_LENGTH     (4)
#define JPEG_EOI_MARKER                 (0xFFD9)
#define HIBYTE(x) (((x) >> 8) & 0xFF)
#define LOBYTE(x) ((x) & 0xFF)

//TODO: This values will be changed
#define BACK_CAMERA_AUTO_FOCUS_DISTANCES_STR       "0.10,1.20,Infinity"
#define FRONT_CAMERA_FOCUS_DISTANCES_STR           "0.20,0.25,Infinity"

#define BACK_CAMERA_MACRO_FOCUS_DISTANCES_STR      "0.10,0.20,Infinity"
#define BACK_CAMERA_INFINITY_FOCUS_DISTANCES_STR   "0.10,1.20,Infinity"

#define BACK_CAMERA_FOCUS_DISTANCE_INFINITY        "Infinity"
#define FRONT_CAMERA_FOCUS_DISTANCE_INFINITY       "Infinity"

#define PREVIEW_GSC_NODE_NUM (1)
#define PICTURE_GSC_NODE_NUM (2)
*/
// This hack does two things:
// -- it sets preview to NV21 (YUV420SP)
// -- it sets gralloc to YV12
//
// The reason being: the samsung encoder understands only yuv420sp, and gralloc
// does yv12 and rgb565.  So what we do is we break up the interleaved UV in
// separate V and U planes, which makes preview look good, and enabled the
// encoder as well.
//
// FIXME: Samsung needs to enable support for proper yv12 coming out of the
//        camera, and to fix their video encoder to work with yv12.
// FIXME: It also seems like either Samsung's YUV420SP (NV21) or img's YV12 has
//        the color planes switched.  We need to figure which side is doing it
//        wrong and have the respective party fix it.

#define MAX_NUM_OF_CAMERA 2

namespace android {

static CameraInfo sCameraInfo[] = {
    {
        CAMERA_FACING_BACK,
        BACK_ROTATION  /* orientation */
    },
    {
        CAMERA_FACING_FRONT,
        FRONT_ROTATION  /* orientation */
    }
};

/** Close this device */

static camera_device_t *g_cam_device[MAX_NUM_OF_CAMERA];
static Mutex g_cam_openLock[MAX_NUM_OF_CAMERA];
static Mutex g_cam_previewLock[MAX_NUM_OF_CAMERA];
static Mutex g_cam_recordingLock[MAX_NUM_OF_CAMERA];

enum CAMERA_STATE {
    CAMERA_NONE,
    CAMERA_OPENED,
    CAMERA_RELEASED,
    CAMERA_CLOSED,
    CAMERA_PREVIEW,
    CAMERA_PREVIEWSTOPPED,
    CAMERA_RECORDING,
    CAMERA_RECORDINGSTOPPED,
};

static char *camera_state_enum2str[40] = {
    "NONE",
    "OPENED",
    "RELEASED",
    "CLOSED",
    "PREVIEW_RUNNING",
    "PREVIEW_STOPPED",
    "RECORDING_RUNNING",
    "RECORDING_STOPPED"
};

static Mutex cam_stateLock[3];
static CAMERA_STATE cam_state[3];

static inline ExynosCameraHWImpl *obj(struct camera_device *dev)
{
    return reinterpret_cast<ExynosCameraHWImpl *>(dev->priv);
}

#if 1//mhjang
static inline SecCameraHardware *obj_ext(struct camera_device *dev)
{
    return reinterpret_cast<SecCameraHardware *>(dev->priv);
}
#endif

static int check_camera_state(CAMERA_STATE state, int cameraId)
{
    bool ret = false;
    cam_stateLock[cameraId].lock();

    ALOGD("DEBUG(%s):camera(%d) state(%d) checking...", __func__, cameraId, state);
    switch (state) {
    case CAMERA_NONE:
        ret = true;
        break;
    case CAMERA_OPENED:
        if (cam_state[cameraId] == CAMERA_NONE
            || cam_state[cameraId] == CAMERA_CLOSED)
            ret = true;
        break;
    case CAMERA_RELEASED:
        if (cam_state[cameraId] == state
            || cam_state[cameraId] == CAMERA_OPENED
            || cam_state[cameraId] == CAMERA_PREVIEWSTOPPED)
            ret = true;
        break;
    case CAMERA_CLOSED:
        if (cam_state[cameraId] == state
            || cam_state[cameraId] == CAMERA_OPENED
            || cam_state[cameraId] == CAMERA_PREVIEWSTOPPED
            || cam_state[cameraId] == CAMERA_RELEASED)
            ret = true;
        break;
    case CAMERA_PREVIEW:
        if (cam_state[cameraId] == CAMERA_OPENED
            || cam_state[cameraId] == CAMERA_PREVIEWSTOPPED)
            ret = true;
        break;
    case CAMERA_PREVIEWSTOPPED:
        if (cam_state[cameraId] == state
            || cam_state[cameraId] == CAMERA_OPENED
            || cam_state[cameraId] == CAMERA_PREVIEW
            || cam_state[cameraId] == CAMERA_RECORDINGSTOPPED)
            ret = true;
        if (cam_state[cameraId] == CAMERA_OPENED)
            usleep(100*1000); /* for Preview Thread running */
        break;
    case CAMERA_RECORDING:
        if (cam_state[cameraId] == CAMERA_PREVIEW
            || cam_state[cameraId] == CAMERA_RECORDINGSTOPPED)
            ret = true;
        break;
    case CAMERA_RECORDINGSTOPPED:
        if (cam_state[cameraId] == state
            || cam_state[cameraId] == CAMERA_RECORDING)
            ret = true;
        break;
    default:
        ALOGE("ERR(%s):camera(%d) state(%s) is unknown value",
            __func__, cameraId, camera_state_enum2str[state]);
        ret = false;
        break;
    }

    if (ret == true)
        ALOGD("DEBUG(%s):camera(%d) state(%d:%s->%d:%s) is valid",
                __func__, cameraId,
                cam_state[cameraId], camera_state_enum2str[cam_state[cameraId]],
                state, camera_state_enum2str[state]);
    else
        ALOGE("ERR(%s):camera(%d) state(%d:%s->%d:%s) is INVALID",
                __func__, cameraId,
                cam_state[cameraId], camera_state_enum2str[cam_state[cameraId]],
                state, camera_state_enum2str[state]);

    cam_stateLock[cameraId].unlock();
    return ret;
}

static int HAL_camera_device_close(struct hw_device_t* device)
{
    int cameraId;
    enum CAMERA_STATE state;

#if 0//mhjang 
if(cameraId == 0) //rear camera(4ec)
{
	///if (!g_cam_device)//mhjang del
        ///ALOGI("camera device already closed");

    if (device) {
        camera_device_t *cam_device = (camera_device_t *)device;
        ALOGI("camera device closed %d", obj_ext(cam_device)->getCameraId());
        delete static_cast<SecCameraHardware *>(cam_device->priv);
        free(cam_device);
        ///g_cam_device = 0;//mhjang
        g_cam_device[cameraId] = NULL;        
    }
}
else
#endif	
{
    ALOGI("[INFO] (%s:%d): in", __func__, __LINE__);
    if (device) {
        camera_device_t *cam_device = (camera_device_t *)device;
        cameraId = obj(cam_device)->getCameraId();

        ALOGI("[INFO] (%s:%d):camera(%d)", __func__, __LINE__, cameraId);

        state = CAMERA_CLOSED;
        if (check_camera_state(state, cameraId) == false) {
            ALOGE("ERR(%s):camera(%d) state(%d) is INVALID", __func__, cameraId, state);
            return -1;
        }

        g_cam_openLock[cameraId].lock();
        ALOGI("[INFO] (%s:%d):camera(%d) locked..", __func__, __LINE__, cameraId);
        g_cam_device[cameraId] = NULL;
        g_cam_openLock[cameraId].unlock();
        ALOGI("[INFO] (%s:%d):camera(%d) unlocked..", __func__, __LINE__, cameraId);

        delete static_cast<ExynosCameraHWImpl *>(cam_device->priv);
        free(cam_device);

        cam_stateLock[cameraId].lock();
        cam_state[cameraId] = state;
        cam_stateLock[cameraId].unlock();
        ALOGI("[INFO] (%s:%d):camera(%d)", __func__, __LINE__, cameraId);
    }

    ALOGI("[INFO] (%s:%d): out", __func__, __LINE__);
}

    return 0;
}

/** Set the preview_stream_ops to which preview frames are sent */
static int HAL_camera_device_set_preview_window(struct camera_device *dev,
                                                struct preview_stream_ops *buf)
{
    static int ret;
    int cameraId = obj(dev)->getCameraId();

    if(cameraId == 0) //rear camera(4ec)
    {
    	HALDBG("%s", __FUNCTION__);
    	return obj_ext(dev)->setPreviewWindow(buf);
    }
    else
    {
    	ALOGI("[INFO] (%s:%d):camera(%d) in", __func__, __LINE__, cameraId);
   	 ret = obj(dev)->setPreviewWindowLocked(buf);
    	ALOGI("[INFO] (%s:%d):camera(%d) out", __func__, __LINE__, cameraId);
    	return ret;
    }

}

/** Set the notification and data callbacks */
static void HAL_camera_device_set_callbacks(struct camera_device *dev,
        camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void* user)
{
    ALOGV("DEBUG(%s):", __func__);

int cameraId = obj(dev)->getCameraId();

if(cameraId == 0) //rear camera(4ec)
{
	obj_ext(dev)->setCallbacks(notify_cb, data_cb, data_cb_timestamp,
							  get_memory,
							  user);
}
else
{
    obj(dev)->setCallbacks(notify_cb, data_cb, data_cb_timestamp,
                           get_memory,
                           user);
}

}

/**
 * The following three functions all take a msg_type, which is a bitmask of
 * the messages defined in include/ui/Camera.h
 */

/**
 * Enable a message, or set of messages.
 */
static void HAL_camera_device_enable_msg_type(struct camera_device *dev, int32_t msg_type)
{
    ALOGV("DEBUG(%s):", __func__);

	int cameraId = obj(dev)->getCameraId();

if(cameraId == 0) //rear camera(4ec)
	obj_ext(dev)->enableMsgType(msg_type);
else
    obj(dev)->enableMsgType(msg_type);


}

/**
 * Disable a message, or a set of messages.
 *
 * Once received a call to disableMsgType(CAMERA_MSG_VIDEO_FRAME), camera
 * HAL should not rely on its client to call releaseRecordingFrame() to
 * release video recording frames sent out by the cameral HAL before and
 * after the disableMsgType(CAMERA_MSG_VIDEO_FRAME) call. Camera HAL
 * clients must not modify/access any video recording frame after calling
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME).
 */
static void HAL_camera_device_disable_msg_type(struct camera_device *dev, int32_t msg_type)
{
    ALOGV("DEBUG(%s):", __func__);

	int cameraId = obj(dev)->getCameraId();

if(cameraId == 0) //rear camera(4ec)
	obj_ext(dev)->disableMsgType(msg_type);
else
    obj(dev)->disableMsgType(msg_type);

}

/**
 * Query whether a message, or a set of messages, is enabled.  Note that
 * this is operates as an AND, if any of the messages queried are off, this
 * will return false.
 */
static int HAL_camera_device_msg_type_enabled(struct camera_device *dev, int32_t msg_type)
{
    ALOGV("DEBUG(%s):", __func__);

	int cameraId = obj(dev)->getCameraId();

if(cameraId == 0) //rear camera(4ec)
	return obj_ext(dev)->msgTypeEnabled(msg_type);
else
    return obj(dev)->msgTypeEnabled(msg_type);


}

/**
 * Start preview mode.
 */
static int HAL_camera_device_start_preview(struct camera_device *dev)
{
    static int ret;
    int cameraId = obj(dev)->getCameraId();
    enum CAMERA_STATE state;

if(cameraId == 0) //rear camera(4ec)
{
	HALDBG("%s", __FUNCTION__);
	state = CAMERA_PREVIEW;
    if (check_camera_state(state, cameraId) == false) {
        ALOGE("ERR(%s):camera(%d) state(%d) is INVALID", __func__, cameraId, state);
        return -1;
    }

    ret = obj_ext(dev)->startPreview();
	if (ret == OK) {
		cam_state[cameraId] = state;
	}
	return ret;

}
else
{
    ALOGI("[INFO] (%s:%d):camera(%d) in", __func__, __LINE__, cameraId);

    state = CAMERA_PREVIEW;
    if (check_camera_state(state, cameraId) == false) {
        ALOGE("ERR(%s):camera(%d) state(%d) is INVALID", __func__, cameraId, state);
        return -1;
    }

    g_cam_previewLock[cameraId].lock();

    ret = obj(dev)->startPreviewLocked();
    ALOGI("[INFO] (%s:%d):camera(%d) out from startPreviewLocked()", __func__, __LINE__, cameraId);

    g_cam_previewLock[cameraId].unlock();

    ALOGI("[INFO] (%s:%d):camera(%d) unlocked..", __func__, __LINE__, cameraId);

    if (ret == OK) {
        cam_stateLock[cameraId].lock();
        cam_state[cameraId] = state;
        cam_stateLock[cameraId].unlock();
        ALOGI("[INFO] (%s:%d):camera(%d) out (startPreview succeeded)", __func__, __LINE__, cameraId);
    } else {
        ALOGI("[INFO] (%s:%d):camera(%d) out (startPreview FAILED)", __func__, __LINE__, cameraId);
    }
    return ret;
}

	
}

/**
 * Stop a previously started preview.
 */
static void HAL_camera_device_stop_recording(struct camera_device *dev);
static void HAL_camera_device_stop_preview(struct camera_device *dev)
{
    int cameraId = obj(dev)->getCameraId();
    enum CAMERA_STATE state;


if(cameraId == 0) //rear camera(4ec)
{
	 HALDBG("%s", __FUNCTION__);
	  state = CAMERA_PREVIEWSTOPPED;
    if (check_camera_state(state, cameraId) == false) {
        ALOGE("ERR(%s):camera(%d) state(%d) is INVALID", __func__, cameraId, state);
        return;
    }

    obj_ext(dev)->stopPreview();
	cam_state[cameraId] = state;
}
else
{
    ALOGI("[INFO] (%s:%d):camera(%d) in", __func__, __LINE__, cameraId);
/* HACK : If camera in recording state, */
/*        CameraService have to call the stop_recording before the stop_preview */
#if 1
    if (cam_state[cameraId] == CAMERA_RECORDING) {
        ALOGE("(%s:%d) camera(%d) in RECORDING RUNNING state ---- INVALID ----", __func__, __LINE__, cameraId);
        ALOGE("(%s:%d) camera(%d) The stop_recording must be called "
                "before the stop_preview  ---- INVALID ----",
                __func__, __LINE__,  cameraId);
        HAL_camera_device_stop_recording(dev);
        ALOGE("(%s:%d) cameraId=%d out from stop_recording  ---- INVALID ----",
                __func__, __LINE__,  cameraId);

        for (int i=0; i<30; i++) {
            ALOGE("(%s:%d) camera(%d) The stop_recording must be called "
                    "before the stop_preview  ---- INVALID ----",
                    __func__, __LINE__,  cameraId);
        }
        ALOGE("(%s:%d) camera(%d) sleep 500ms for ---- INVALID ---- state", __func__, __LINE__,  cameraId);
        usleep(500000); /* to notify, sleep 500ms */
    }
#endif
    state = CAMERA_PREVIEWSTOPPED;
    if (check_camera_state(state, cameraId) == false) {
        ALOGE("ERR(%s):camera(%d) state(%d) is INVALID", __func__, cameraId, state);
        return;
    }

    g_cam_previewLock[cameraId].lock();

    obj(dev)->stopPreviewLocked();
    ALOGI("[INFO] (%s:%d):camera(%d) out from stopPreviewLocked()", __func__, __LINE__, cameraId);

    g_cam_previewLock[cameraId].unlock();

    ALOGI("[INFO] (%s:%d):camera(%d) unlocked..", __func__, __LINE__, cameraId);

    cam_stateLock[cameraId].lock();
    cam_state[cameraId] = state;
    cam_stateLock[cameraId].unlock();
    ALOGI("[INFO] (%s:%d):camera(%d) out", __func__, __LINE__, cameraId);
}

}

/**
 * Returns true if preview is enabled.
 */
static int HAL_camera_device_preview_enabled(struct camera_device *dev)
{
    ALOGV("DEBUG(%s):", __func__);

int cameraId = obj(dev)->getCameraId();

if(cameraId == 0) //rear camera(4ec)
	return obj_ext(dev)->previewEnabled();
else
    return obj(dev)->previewEnabled();

}

/**
 * Request the camera HAL to store meta data or real YUV data in the video
 * buffers sent out via CAMERA_MSG_VIDEO_FRAME for a recording session. If
 * it is not called, the default camera HAL behavior is to store real YUV
 * data in the video buffers.
 *
 * This method should be called before startRecording() in order to be
 * effective.
 *
 * If meta data is stored in the video buffers, it is up to the receiver of
 * the video buffers to interpret the contents and to find the actual frame
 * data with the help of the meta data in the buffer. How this is done is
 * outside of the scope of this method.
 *
 * Some camera HALs may not support storing meta data in the video buffers,
 * but all camera HALs should support storing real YUV data in the video
 * buffers. If the camera HAL does not support storing the meta data in the
 * video buffers when it is requested to do do, INVALID_OPERATION must be
 * returned. It is very useful for the camera HAL to pass meta data rather
 * than the actual frame data directly to the video encoder, since the
 * amount of the uncompressed frame data can be very large if video size is
 * large.
 *
 * @param enable if true to instruct the camera HAL to store
 *      meta data in the video buffers; false to instruct
 *      the camera HAL to store real YUV data in the video
 *      buffers.
 *
 * @return OK on success.
 */
static int HAL_camera_device_store_meta_data_in_buffers(struct camera_device *dev, int enable)
{
    ALOGV("DEBUG(%s):", __func__);

	int cameraId = obj(dev)->getCameraId();

///if(cameraId == 0) //rear camera(4ec)
///	return obj_ext(dev)->storeMetaDataInBuffers(enable);
///else
    return obj(dev)->storeMetaDataInBuffers(enable);

}

/**
 * Start record mode. When a record image is available, a
 * CAMERA_MSG_VIDEO_FRAME message is sent with the corresponding
 * frame. Every record frame must be released by a camera HAL client via
 * releaseRecordingFrame() before the client calls
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME). After the client calls
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME), it is the camera HAL's
 * responsibility to manage the life-cycle of the video recording frames,
 * and the client must not modify/access any video recording frames.
 */
static int HAL_camera_device_start_recording(struct camera_device *dev)
{
    static int ret;
    int cameraId = obj(dev)->getCameraId();
    enum CAMERA_STATE state;

    ALOGI("[INFO] (%s:%d):camera(%d) in", __func__, __LINE__, cameraId);
 
    state = CAMERA_RECORDING;
    if (check_camera_state(state, cameraId) == false) {
        ALOGE("ERR(%s):camera(%d) state(%d) is INVALID", __func__, cameraId, state);
        return -1;
    }

    g_cam_recordingLock[cameraId].lock();

    ret = obj(dev)->startRecording();
    ALOGI("[INFO] (%s:%d):camera(%d) out from startRecording()", __func__, __LINE__, cameraId);

    g_cam_recordingLock[cameraId].unlock();

    ALOGI("[INFO] (%s:%d):camera(%d) unlocked..", __func__, __LINE__, cameraId);

    if (ret == OK) {
        cam_stateLock[cameraId].lock();
        cam_state[cameraId] = state;
        cam_stateLock[cameraId].unlock();
        ALOGI("[INFO] (%s:%d):camera(%d) out (startRecording succeeded)", __func__, __LINE__, cameraId);
    } else {
        ALOGI("[INFO] (%s:%d):camera(%d) out (startRecording FAILED)", __func__, __LINE__, cameraId);
    }
    return ret;
}

/**
 * Stop a previously started recording.
 */
static void HAL_camera_device_stop_recording(struct camera_device *dev)
{
    int cameraId = obj(dev)->getCameraId();
    enum CAMERA_STATE state;

    ALOGI("[INFO] (%s:%d):camera(%d) in", __func__, __LINE__, cameraId);

    state = CAMERA_RECORDINGSTOPPED;
    if (check_camera_state(state, cameraId) == false) {
        ALOGE("ERR(%s):camera(%d) state(%d) is INVALID", __func__, cameraId, state);
        return;
    }

    g_cam_recordingLock[cameraId].lock();

    obj(dev)->stopRecording();
    ALOGI("[INFO] (%s:%d):camera(%d) out from stopRecording()", __func__, __LINE__, cameraId);

    g_cam_recordingLock[cameraId].unlock();

    ALOGI("[INFO] (%s:%d):camera(%d) unlocked..", __func__, __LINE__, cameraId);

    cam_stateLock[cameraId].lock();
    cam_state[cameraId] = state;
    cam_stateLock[cameraId].unlock();
    ALOGI("[INFO] (%s:%d):camera(%d) out", __func__, __LINE__, cameraId);
}

/**
 * Returns true if recording is enabled.
 */
static int HAL_camera_device_recording_enabled(struct camera_device *dev)
{
    ALOGV("DEBUG(%s):", __func__);
    return obj(dev)->recordingEnabled();
}

/**
 * Release a record frame previously returned by CAMERA_MSG_VIDEO_FRAME.
 *
 * It is camera HAL client's responsibility to release video recording
 * frames sent out by the camera HAL before the camera HAL receives a call
 * to disableMsgType(CAMERA_MSG_VIDEO_FRAME). After it receives the call to
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME), it is the camera HAL's
 * responsibility to manage the life-cycle of the video recording frames.
 */
static void HAL_camera_device_release_recording_frame(struct camera_device *dev,
                                const void *opaque)
{
    ALOGV("DEBUG(%s):", __func__);
    obj(dev)->releaseRecordingFrame(opaque);
}

/**
 * Start auto focus, the notification callback routine is called with
 * CAMERA_MSG_FOCUS once when focusing is complete. autoFocus() will be
 * called again if another auto focus is needed.
 */
static int HAL_camera_device_auto_focus(struct camera_device *dev)
{
    ALOGV("DEBUG(%s):", __func__);

int cameraId = obj(dev)->getCameraId();

if(cameraId == 0) //rear camera(4ec)
	return obj_ext(dev)->autoFocus();
else	
    return obj(dev)->autoFocus();

}

/**
 * Cancels auto-focus function. If the auto-focus is still in progress,
 * this function will cancel it. Whether the auto-focus is in progress or
 * not, this function will return the focus position to the default.  If
 * the camera does not support auto-focus, this is a no-op.
 */
static int HAL_camera_device_cancel_auto_focus(struct camera_device *dev)
{
    ALOGV("DEBUG(%s):", __func__);

	int cameraId = obj(dev)->getCameraId();

if(cameraId == 0) //rear camera(4ec)
	return obj_ext(dev)->cancelAutoFocus();
else
    return obj(dev)->cancelAutoFocus();

}

/**
 * Take a picture.
 */
static int HAL_camera_device_take_picture(struct camera_device *dev)
{
    ALOGV("DEBUG(%s):", __func__);

	int cameraId = obj(dev)->getCameraId();

if(cameraId == 0) //rear camera(4ec)
	return obj_ext(dev)->takePicture();
else
    return obj(dev)->takePicture();

}

/**
 * Cancel a picture that was started with takePicture. Calling this method
 * when no picture is being taken is a no-op.
 */
static int HAL_camera_device_cancel_picture(struct camera_device *dev)
{
    ALOGV("DEBUG(%s):", __func__);
	int cameraId = obj(dev)->getCameraId();

if(cameraId == 0) //rear camera(4ec)
	return obj_ext(dev)->cancelPicture();
else
    return obj(dev)->cancelPicture();

}

/**
 * Set the camera parameters. This returns BAD_VALUE if any parameter is
 * invalid or not supported.
 */
static int HAL_camera_device_set_parameters(struct camera_device *dev,
                                            const char *parms)
{
    ALOGV("DEBUG(%s):", __func__);
    String8 str(parms);
    CameraParameters p(str);

	int cameraId = obj(dev)->getCameraId(); //mhjang add

if(cameraId == 0) //rear camera(4ec)
{
	return obj_ext(dev)->setParameters(p);

}
else
{
    return obj(dev)->setParametersLocked(p);
}
	
}

/** Return the camera parameters. */
char *HAL_camera_device_get_parameters(struct camera_device *dev)
{
    ALOGV("DEBUG(%s):", __func__);
    String8 str;

///int cameraId = obj(dev)->getCameraId(); //mhjang add

///if(cameraId == 0)  //rear camera(4ec)
///	CameraParameters parms = obj_ext(dev)->getParameters();
///else
    CameraParameters parms = obj(dev)->getParameters();

    str = parms.flatten();
    return strdup(str.string());
}

static void HAL_camera_device_put_parameters(struct camera_device *dev, char *parms)
{
    ALOGV("DEBUG(%s):", __func__);
    free(parms);
}

/**
 * Send command to camera driver.
 */
static int HAL_camera_device_send_command(struct camera_device *dev,
                    int32_t cmd, int32_t arg1, int32_t arg2)
{
    ALOGV("DEBUG(%s):", __func__);

	int cameraId = obj(dev)->getCameraId(); //mhjang add

if(cameraId == 0) //rear camera(4ec)
	return obj_ext(dev)->sendCommand(cmd, arg1, arg2);
else
    return obj(dev)->sendCommand(cmd, arg1, arg2);
}

/**
 * Release the hardware resources owned by this object.  Note that this is
 * *not* done in the destructor.
 */
static void HAL_camera_device_release(struct camera_device *dev)
{
    int cameraId = obj(dev)->getCameraId();
    enum CAMERA_STATE state;


if(cameraId == 0) //rear camera(4ec)
{
	HALDBG("%s", __FUNCTION__);
	state = CAMERA_RELEASED;
    if (check_camera_state(state, cameraId) == false) {
        ALOGE("ERR(%s):camera(%d) state(%d) is INVALID", __func__, cameraId, state);
        return;
    }
		obj_ext(dev)->release();
		cam_state[cameraId] = state;
}
else
{
    ALOGI("[INFO] (%s:%d):camera(%d) in", __func__, __LINE__, cameraId);

    state = CAMERA_RELEASED;
    if (check_camera_state(state, cameraId) == false) {
        ALOGE("ERR(%s):camera(%d) state(%d) is INVALID", __func__, cameraId, state);
        return;
    }

    g_cam_openLock[cameraId].lock();

    obj(dev)->release();
    ALOGI("[INFO] (%s:%d):camera(%d) out from release()", __func__, __LINE__, cameraId);

    g_cam_openLock[cameraId].unlock();

    ALOGI("[INFO] (%s:%d):camera(%d) unlocked..", __func__, __LINE__, cameraId);

    cam_stateLock[cameraId].lock();
    cam_state[cameraId] = state;
    cam_stateLock[cameraId].unlock();
    ALOGI("[INFO] (%s:%d):camera(%d) out", __func__, __LINE__, cameraId);
}

}

/**
 * Dump state of the camera hardware
 */
static int HAL_camera_device_dump(struct camera_device *dev, int fd)
{
    ALOGV("DEBUG(%s):", __func__);

	int cameraId = obj(dev)->getCameraId(); //mhjang add

if(cameraId == 0) //rear camera(4ec)
	return obj_ext(dev)->dump(fd);
else
    return obj(dev)->dump(fd);
}

static int HAL_getNumberOfCameras()
{
    ALOGV("DEBUG(%s):", __func__);
    return sizeof(sCameraInfo) / sizeof(sCameraInfo[0]);
}

static int HAL_getCameraInfo(int cameraId, struct camera_info *info)
{
    ALOGV("DEBUG(%s):", __func__);
    if (cameraId < 0 || cameraId >= HAL_getNumberOfCameras()) {
        ALOGE("ERR(%s):Invalid camera ID %d", __func__, cameraId);
        return -EINVAL;
    }

    memcpy(info, &sCameraInfo[cameraId], sizeof(CameraInfo));
    info->device_version = HARDWARE_DEVICE_API_VERSION(1, 0);

    return NO_ERROR;
}


#define SET_METHOD(m) m : HAL_camera_device_##m

static camera_device_ops_t camera_device_ops = {
        SET_METHOD(set_preview_window),
        SET_METHOD(set_callbacks),
        SET_METHOD(enable_msg_type),
        SET_METHOD(disable_msg_type),
        SET_METHOD(msg_type_enabled),
        SET_METHOD(start_preview),
        SET_METHOD(stop_preview),
        SET_METHOD(preview_enabled),
        SET_METHOD(store_meta_data_in_buffers),
        SET_METHOD(start_recording),
        SET_METHOD(stop_recording),
        SET_METHOD(recording_enabled),
        SET_METHOD(release_recording_frame),
        SET_METHOD(auto_focus),
        SET_METHOD(cancel_auto_focus),
        SET_METHOD(take_picture),
        SET_METHOD(cancel_picture),
        SET_METHOD(set_parameters),
        SET_METHOD(get_parameters),
        SET_METHOD(put_parameters),
        SET_METHOD(send_command),
        SET_METHOD(release),
        SET_METHOD(dump),
};

#undef SET_METHOD

static int HAL_camera_device_open(const struct hw_module_t* module,
                                  const char *id,
                                  struct hw_device_t** device)
{
    int cameraId = atoi(id);
    enum CAMERA_STATE state;

    ALOGI("[INFO] (%s:%d):camera(%d) in", __func__, __LINE__, cameraId);
    if (cameraId < 0 || cameraId >= HAL_getNumberOfCameras()) {
        ALOGE("ERR(%s):Invalid camera ID %s", __func__, id);
        return -EINVAL;
    }

    state = CAMERA_OPENED;

    if (check_camera_state(state, cameraId) == false) {
        ALOGE("ERR(%s):camera(%d) state(%d) is INVALID", __func__, cameraId, state);
        return -1;
    }

    if (cameraId < (sizeof(sCameraInfo) / sizeof(sCameraInfo[0]))) {
        if (g_cam_device[cameraId]) {
            ALOGE("DEBUG(%s):returning existing camera ID %s", __func__, id);
            *device = (hw_device_t *)g_cam_device[cameraId];
            goto done;
        }

        g_cam_device[cameraId] = (camera_device_t *)malloc(sizeof(camera_device_t));
        if (!g_cam_device[cameraId])
            return -ENOMEM;

        g_cam_openLock[cameraId].lock();
        g_cam_device[cameraId]->common.tag     = HARDWARE_DEVICE_TAG;
        g_cam_device[cameraId]->common.version = 1;
        g_cam_device[cameraId]->common.module  = const_cast<hw_module_t *>(module);
        g_cam_device[cameraId]->common.close   = HAL_camera_device_close;

        g_cam_device[cameraId]->ops = &camera_device_ops;

        ALOGE("DEBUG(%s):open camera %s", __func__, id);
	
#if 1//mhjang	
	if(cameraId == 0) //rear camera(4ec)
        	g_cam_device[cameraId]->priv = new SecCameraHardware(cameraId, g_cam_device[cameraId]);
	else
		g_cam_device[cameraId]->priv = new ExynosCameraHWImpl(cameraId, g_cam_device[cameraId]);
#else
	g_cam_device[cameraId]->priv = new ExynosCameraHWImpl(cameraId, g_cam_device[cameraId]);
#endif
		
        *device = (hw_device_t *)g_cam_device[cameraId];
        ALOGI("[INFO] (%s:%d):camera(%d) out from new g_cam_device[%d]->priv()", __func__, __LINE__, cameraId, cameraId);

        g_cam_openLock[cameraId].unlock();
        ALOGI("[INFO] (%s:%d):camera(%d) unlocked..", __func__, __LINE__, cameraId);
    } else {
        ALOGE("DEBUG(%s):camera(%s) open fail - must front camera open first", __func__, id);
        return -EINVAL;
    }

done:
    cam_stateLock[cameraId].lock();
    cam_state[cameraId] = state;
    cam_stateLock[cameraId].unlock();
    ALOGI("[INFO] (%s:%d):camera(%d) out", __func__, __LINE__, cameraId);
    return 0;
}

static hw_module_methods_t camera_module_methods = {
            open : HAL_camera_device_open
};

extern "C" {
    struct camera_module HAL_MODULE_INFO_SYM = {
      common : {
          tag                : HARDWARE_MODULE_TAG,
          module_api_version : CAMERA_MODULE_API_VERSION_1_0,
          hal_api_version    : HARDWARE_HAL_API_VERSION,
          id                 : CAMERA_HARDWARE_MODULE_ID,
          name               : "Exynos Camera HAL1",
          author             : "Samsung Corporation",
          methods            : &camera_module_methods,
          dso                : NULL,
          reserved           : {0},
      },
      get_number_of_cameras : HAL_getNumberOfCameras,
      get_camera_info       : HAL_getCameraInfo
    };
}

}; // namespace android

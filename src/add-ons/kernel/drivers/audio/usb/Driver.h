/*
 *	Driver for USB Audio Device Class devices.
 *	Copyright (c) 2009-13 S.Zharski <imker@gmx.li>
 *	Distributed under the terms of the MIT license.
 *
 */
#ifndef _USB_AUDIO_DRIVER_H_
#define _USB_AUDIO_DRIVER_H_


#include <Drivers.h>
#include <dm2/bus/USB.h>


#define DRIVER_NAME	"usb_audio"

const char* const kVersion = "ver.0.0.5";

// initial buffer size in samples
const uint32 kSamplesBufferSize = 1024;
// [sub]buffers count
const uint32 kSamplesBufferCount = 2;


#endif // _USB_AUDIO_DRIVER_H_


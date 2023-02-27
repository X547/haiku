#include <KernelExport.h>
#include <fs_interface.h>
#include <AutoDeleter.h>
#include <AutoDeleterPosix.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include <port.h>
#include "device_manager/IORequest.h"

#pragma GCC diagnostic ignored "-Wdelete-non-virtual-dtor"


struct TestPortWriteCallback : PortWriteCallback {
	int32 fOrd;

	TestPortWriteCallback(int32 old);

	bool Do(BReferenceable *port) final;
};


struct TestPortReadCallback : PortReadCallback {
	void Do(BReferenceable *port) final;
};


static thread_id sThread = B_ERROR;
static port_id sPort = B_ERROR;


TestPortWriteCallback::TestPortWriteCallback(int32 ord):
	fOrd(ord)
{}

bool
TestPortWriteCallback::Do(BReferenceable *port)
{
	char buf[256];
	dprintf("TestPortWriteCallback::Do, seq: %" B_PRId32 "\n", fSeq);
	sprintf(buf, "TestPortWriteCallback(ord: %" B_PRId32 ", seq: %" B_PRId32 ")", fOrd, fSeq);
	TestPortReadCallback *readCallback = new TestPortReadCallback();
	Write(port, 4321, buf, strlen(buf) + 1, readCallback);
	if (fOrd >= 100) {
		delete this;
		return false;
	}
	fOrd++;
	return true;
}


void
TestPortReadCallback::Do(BReferenceable *port)
{
	dprintf("TestPortReadCallback::Do, seq: %" B_PRId32 "\n", fSeq);
	delete this;
}


static
status_t ThreadEntry(void *arg)
{
	for (;;) {
		int32 msgCode;
		ssize_t bufSize;
		bufSize = port_buffer_size(sPort);
		if (bufSize == B_BAD_PORT_ID)
			break;
		if (bufSize < 0)
			continue;
		ArrayDeleter<uint8> buf(new uint8[bufSize]);
		if (read_port(sPort, &msgCode, &buf[0], bufSize) < B_OK)
			panic("read_port failed");
		dprintf("message(%" B_PRIu32 ": \"%s\")\n", msgCode, (const char*)&buf[0]);
	}
	return B_OK;
}


static void test_port()
{
	sPort = create_port(10, "test port");
#if 0
	const char str[] = "This is a test.";
	write_port(sPort, 1234, str, sizeof(str));
#endif
	for (int32 i = 0; i < 1; i++) {
		TestPortWriteCallback *callback = new(std::nothrow) TestPortWriteCallback(i);
		add_port_write_callback(sPort, callback);
	}

	sThread = spawn_kernel_thread(ThreadEntry, "test thread", B_NORMAL_PRIORITY, NULL);
	status_t res;
	wait_for_thread(sThread, &res);
}


struct TestAsyncRequest {
	IORequest fIoReq;
	int fFd = -1;
	off_t fOffset = 0;
	ArrayDeleter<uint8> fBuffer;
	size_t fBufferSize = 0;

	void Do();
	void Completed(status_t status, bool partialTransfer, generic_size_t transferEndOffset);
};


void TestAsyncRequest::Do()
{
	fIoReq.Init(fOffset, (generic_addr_t)&fBuffer[0], fBufferSize, false, 0);
	fIoReq.SetFinishedCallback(
		[](
			void* data,
			io_request* request, status_t status, bool partialTransfer,
			generic_size_t transferEndOffset
		) {
			static_cast<TestAsyncRequest*>(data)->Completed(status, partialTransfer, transferEndOffset);
			return B_OK;
		},
		this
	);
	do_fd_io(fFd, &fIoReq);
}

void TestAsyncRequest::Completed(status_t status, bool partialTransfer, generic_size_t transferEndOffset)
{
	dprintf("TestAsyncRequest::Completed, offset: %#" B_PRIx64 ", data: %#" B_PRIx32 "\n", fOffset, *(uint32*)&fBuffer[0]);
	if (status >= B_OK && !partialTransfer) {
		fOffset += fBufferSize;
		fIoReq.~IORequest();
		new (&fIoReq) IORequest();
		Do();
	}
}


static void test_async_io()
{
	//const char path[] = "/dev/disk/virtual/virtio_block/0/raw";
	const char path[] = "/boot/system/kernel_riscv64";
	FileDescriptorCloser fd(open(path, O_RDONLY));

	TestAsyncRequest req;
	req.fFd = fd.Get();
	req.fBufferSize = 0x10000;
	req.fBuffer.SetTo(new(std::nothrow) uint8[req.fBufferSize]);
	req.Do();

	sPort = create_port(10, "test port");
	sThread = spawn_kernel_thread(ThreadEntry, "test thread", B_NORMAL_PRIORITY, NULL);
	status_t res;
	wait_for_thread(sThread, &res);
}


void
do_watch_objects_test()
{
	switch (1) {
		case 1:
			test_port();
			break;
		case 2:
			test_async_io();
			break;
	}
	panic("do_watch_objects_test");
}

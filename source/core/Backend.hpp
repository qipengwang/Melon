//
//  Backend.hpp
//  MNN
//
//  Created by MNN on 2018/07/06.
//  Copyright © 2018, Alibaba Group Holding Limited
//

#ifndef Backend_hpp
#define Backend_hpp

#include <MNN/MNNForwardType.h>
#include <stdio.h>
#include <MNN/ErrorCode.hpp>
#include <MNN/Tensor.hpp>
#include <map>
#include <memory>
#include <vector>
#include <set>
#include "Command.hpp"
#include "NonCopyable.hpp"

// threshold of hybrid dynamic allocate buffer, only used by ops' outputs, default 4MB == 4*1024*1024 == 1<<22
// if(size < threshold): from memory pool
// else: from os
#define MNN_HYBRID_DYNAMIC_THRESHOLD 1<<22

namespace MNN {

struct Op;
struct GpuLibrary;
class Execution;

class Runtime;
/** abstract backend */
class Backend : public NonCopyable {
public:
    /** info used to create backend */
    struct Info {
        /** forward type. */
        MNNForwardType type = MNN_FORWARD_CPU;
        /** for CPU only. number of threads. */
        int numThread = 4;
        /** user data. */
        BackendConfig* user = NULL;
        enum Mode {
            // The Op will be run in execution->onExecute
            DIRECT = 0,

            // The Op will be recorded. Run in onExecuteBegin and Wait in onExecuteEnd
            INDIRECT = 1
        };
        Mode mode = DIRECT;
    };

    /** backend buffer storage type */
    enum StorageType {
        /**
         use NOT reusable memory.
         - allocates memory when `onAcquireBuffer` is called.
         - releases memory when `onReleaseBuffer` is called or when the backend is deleted.
         - do NOTHING when `onClearBuffer` is called.
         */
        STATIC,
        /**
         use reusable memory.
         - allocates or reuses memory when `onAcquireBuffer` is called. prefers reusing.
         - collects memory for reuse when `onReleaseBuffer` is called.
         - releases memory when `onClearBuffer` is called or when the backend is deleted.
         */
        DYNAMIC,
        /**
         use NOT reusable memory.
         - allocates memory when `onAcquireBuffer` is called.
         - do NOTHING when `onReleaseBuffer` is called.
         - releases memory when `onClearBuffer` is called or when the backend is deleted.
         */
        DYNAMIC_SEPERATE
    };


    enum BufferType {
        DYNAMIC_OTHER,
        DYNAMIC_OUTPUT,
        DYNAMIC_RESIZE
    };

public:
    /**
     * @brief initializer.
     * @param type  forward type.
     */
    Backend(MNNForwardType type) : mType(type) {
        // nothing to do
    }

    /**
     * @brief deinitializer.
     */
    virtual ~Backend() = default;

public:
    /**
     * @brief measure the cost for op with input and output tensors.
     * @param inputs    input tensors.
     * @param outputs   output tensors.
     * @param op        given op.
     * @return std::make_pair(timeDelayInMs, support);
     */
    virtual std::pair<float, bool> onMeasure(const std::vector<Tensor*>& inputs, const std::vector<Tensor*>& outputs,
                                             const MNN::Op* op) {
        return std::make_pair(0.0f, false);
    }

    /**
     * @brief create execution for op with input and output tensors.
     * @param inputs    input tensors.
     * @param outputs   output tensors.
     * @param op        given op.
     * @return created execution if op is supported, nullptr otherwise.
     */
    virtual Execution* onCreate(const std::vector<Tensor*>& inputs, const std::vector<Tensor*>& outputs,
                                const MNN::Op* op) = 0;

    /**
     * @brief callback before resize ops.
     */
    virtual void onResizeBegin() {
        // nothing to do
    }
    /**
     * @brief callback after resize ops.
     */
    virtual void onResizeEnd() {
        // nothing to do
    }

    /**
     * @brief callback before executing ops.
     */
    virtual void onExecuteBegin() const = 0;
    /**
     * @brief callback after executing ops.
     */
    virtual void onExecuteEnd() const = 0;

    virtual std::vector<Tensor*> moveTensor2bottom(std::vector<Tensor*>, size_t budget_new) = 0;

    virtual bool adaptTensorToNewAddress(std::vector<Tensor*> tensors) = 0;

    virtual size_t usedSize() = 0;

public:
    /**
     * @brief allocate buffer of tensor for given storage type.
     * @param tensor        buffer provider.
     * @param storageType   buffer storage type.
     * @return success or not.
     */
    virtual bool onAcquireBuffer(const Tensor* tensor, StorageType storageType) = 0;

    /**
     * @brief release buffer of tensor for given storage type.
     * @param tensor        buffer provider.
     * @param storageType   buffer storage type.
     * @return success or not.
     */
    virtual bool onReleaseBuffer(const Tensor* tensor, StorageType storageType) = 0;

    /**
     * @brief clear all dynamic buffers.
     * @return success or not.
     */
    virtual bool onClearBuffer() = 0;

    virtual bool onRequireBufferFromOS(const Tensor* tensor)  = 0;
    virtual bool onFreeBufferToOS(const Tensor* tensor)  = 0;
    virtual bool onRequireBufferHybrid(const Tensor* tensor, int hybrid_thres=MNN_HYBRID_DYNAMIC_THRESHOLD) = 0;
    virtual bool onFreeBufferHybrid(const Tensor* tensor, int hybrid_thres=MNN_HYBRID_DYNAMIC_THRESHOLD) = 0;

    BufferType mBufferType = BufferType::DYNAMIC_OTHER;
    virtual void changeBufferType(BufferType bufferType) = 0;
    virtual void setHeuristicStrategy(bool flag, std::string modelName, int batchsize, int bgt, bool alignBottom=false, bool needAlloc=true) = 0;
    /**
     * @brief copy buffer from tensor to tensor.
     * @param srcTensor source buffer provider.
     * @param dstTensor dest buffer provider.
     */
    virtual void onCopyBuffer(const Tensor* srcTensor, const Tensor* dstTensor) const = 0;

public:
    /**
     * @brief get forward type.
     * @return forward type.
     */
    inline MNNForwardType type() const {
        return mType;
    }

private:
    const MNNForwardType mType;
};

/** Each backend belong to a runtime*/
class Runtime : public NonCopyable {
public:
    /**
     Origin Op -> (Compiler) -> New Op -> Backend
     Default use Compiler_Geometry, Origin Op -> Compiler_Geometry -> Little Op
     For serveral Backend, we can't use Geometry to decompose origin op, then it set Compiler_Origin
     */
    enum CompilerType {
        Compiler_Geometry = 0,
        Compiler_Origin = 1,
    };

    virtual CompilerType onGetCompilerType() const {
        return Compiler_Geometry;
    }

    virtual ~Runtime() = default;
    /**
     @brief create backend
     @return created backend
     */
    virtual Backend* onCreate() const = 0;

    /**
     @brief clear unuseful resource
     @param level clear level: 0 - 100, bigger mean clear more, smaller mean cache more
     */
    virtual void onGabageCollect(int level) = 0;

    /**
     @brief Measure the memory it used in MB
     */
    virtual float onGetMemoryInMB() {
        return 0.0f;
    }

    // If buffer is not nullptr, try copy cache, else delete cache
    virtual bool onSetCache(const void* buffer, size_t size) {
        return false;
    }

    virtual std::pair<const void*, size_t> onGetCache() {
        return std::make_pair(nullptr, 0);
    }
};

/** abstract Runtime register */
class RuntimeCreator {
public:
    /**
     @brief initializer.
     */
    virtual ~RuntimeCreator() = default;

    virtual Runtime* onCreate(const Backend::Info& info) const = 0;
    /**
     @brief Turn info to supported.
     @param info    info to valid.
     @return success or not
     */
    virtual bool onValid(Backend::Info& info) const {
        info.mode = Backend::Info::DIRECT;
        return true;
    }

protected:
    /**
     @brief deinitializer.
     */
    RuntimeCreator() = default;
};

/**
 * @brief get registered backend creator for given forward type.
 * @param type  given forward type.
 * @return backend creator pointer if registered, nullptr otherwise.
 */
MNN_PUBLIC const RuntimeCreator* MNNGetExtraRuntimeCreator(MNNForwardType type);

/**
 * @brief register backend creator for given forward type.
 * @param type given forward type.
 * @param creator registering backend creator.
 * @return true if backend creator for given forward type was not registered before, false otherwise.
 */
MNN_PUBLIC bool MNNInsertExtraRuntimeCreator(MNNForwardType type, const RuntimeCreator* creator,
                                             bool needCheck = false);

MNN_PUBLIC bool MNNCPUCopyBuffer(const Tensor* srcTensor, const Tensor* dstTensor);
} // namespace MNN

#endif /* Backend_hpp */

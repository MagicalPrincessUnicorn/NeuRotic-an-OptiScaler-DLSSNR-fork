#pragma once
namespace Neurotic::Semantic::Character {
enum class QueueUpdate {Keep,Uniform,Unsupported};
// ResizeBuffers1 arrays have the caller's extent. Never enlarge that extent,
// and preserve zero (keep existing buffer count) when arrays are supplied.
inline unsigned CharacterResizeBufferCount(unsigned count,bool hasArrays) noexcept {
    return !hasArrays&&count<2?2:count;
}
// Different wrappers may mean the same queue, but do not prove ordering here.
template<class T> QueueUpdate CharacterQueueUpdate(bool succeeded,unsigned count,T* const* queues) noexcept {
    if(!succeeded||!queues)return QueueUpdate::Keep;
    if(!count||count>16||!queues[0])return QueueUpdate::Unsupported;
    for(unsigned i=1;i<count;++i)if(queues[i]!=queues[0])return QueueUpdate::Unsupported;
    return QueueUpdate::Uniform;
}
}

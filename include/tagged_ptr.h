///////////////////////////////////////////////////////////////////////////
//
//tagged_ptr.h
//
/////////////////////////////////////////////////////////////////////////////
#pragma once


namespace lockfree {

//-------------------------------------------------------------------------
// Embeds a tag value in the upper 16-bits of a 64-bit pointer. This works on both x86 and x64 because x64 only uses the 48 less 
// significant bits for virtual addresses (http://en.wikipedia.org/wiki/X86-64#Virtual_address_space_details)
template <typename T>
struct tTaggedPtr
{
public:
	typedef uint16_t tTag;

private:
	typedef uint64_t tPackedPtr;

	union 
	{
		tPackedPtr	mPackedPtr;
		struct
		{
			// This union assumes little-endian
			uint8_t mPadding[sizeof(tPackedPtr) - sizeof(tTag)];

			tTag	mTag;
		};
	};

public:
	tTaggedPtr(std::nullptr_t = nullptr)
		: mPackedPtr()
	{
		static_assert(sizeof(tTaggedPtr) == 8, "tTaggedPtr not properly packed");
	}

	tTaggedPtr(T* ptr, tTag tag = 0U)
	{		
		PackTaggedPtr(ptr, tag);
	}

	T* GetPtr() const
	{
		static constexpr const uint64_t VIRTUAL_ADDRESS_MASK = (1ULL << 48) - 1ULL;
		return reinterpret_cast<T*>(mPackedPtr & VIRTUAL_ADDRESS_MASK);
	}

	tTag GetTag() const
	{
		return mTag;
	}

	void Set(T* ptr, tTag tag)
	{
		PackTaggedPtr(ptr, tag);
	}

	T& operator *() const
	{
		return *GetPtr();
	}

	T* operator->() const
	{
		return GetPtr();
	}

	operator bool() const
	{
		return GetPtr() != nullptr;
	}

private:
	void PackTaggedPtr(T* ptr, tTag tag)
	{
		mPackedPtr = reinterpret_cast<tPackedPtr>(ptr);
		mTag = tag;
	}
};
   
}


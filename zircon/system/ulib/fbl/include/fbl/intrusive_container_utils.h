// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_INTRUSIVE_CONTAINER_UTILS_H_
#define FBL_INTRUSIVE_CONTAINER_UTILS_H_

#include <type_traits>
#include <utility>

#include <fbl/intrusive_pointer_traits.h>
#include <fbl/macros.h>

namespace fbl {

// DefaultKeyedObjectTraits defines a default implementation of traits used to
// manage objects stored in associative containers such as hash-tables and
// trees.
//
// At a minimum, a class or a struct which is to be used to define the
// traits of a keyed object must define the following public members.
//
// GetKey   : A static method which takes a constant reference to an object (the
//            type of which is infered from PtrType) and returns a KeyType
//            instance corresponding to the key for an object.
// LessThan : A static method which takes two keys (key1 and key2) and returns
//            true if-and-only-if key1 is considered to be less than key2 for
//            sorting purposes.
// EqualTo  : A static method which takes two keys (key1 and key2) and returns
//            true if-and-only-if key1 is considered to be equal to key2.
//
// Rules for keys:
// ++ The type of key returned by GetKey must be compatible with the key which
//    was specified for the container.
// ++ The key for an object must remain constant for as long as the object is
//    contained within a container.
// ++ When comparing keys, comparisons must obey basic transative and
//    commutative properties.  That is to say...
//    LessThan(A, B) and LessThan(B, C) implies LessThan(A, C)
//    EqualTo(A, B) and EqualTo(B, C) implies EqualTo(A, C)
//    EqualTo(A, B) if-and-only-if EqualTo(B, A)
//    LessThan(A, B) if-and-only-if EqualTo(B, A) or (not LessThan(B, A))
//
// DefaultKeyedObjectTraits is a helper class which allows an object to be
// treated as a keyed-object by implementing a const GetKey method which returns
// a key of the appropriate type.  The key type must be compatible with the
// container key type, and must have definitions of the < and == operators for
// the purpose of generating implementation of LessThan and EqualTo.
template <typename KeyType, typename ObjType>
struct DefaultKeyedObjectTraits {
  static KeyType GetKey(const ObjType& obj) { return obj.GetKey(); }
  static bool LessThan(const KeyType& key1, const KeyType& key2) { return key1 < key2; }
  static bool EqualTo(const KeyType& key1, const KeyType& key2) { return key1 == key2; }
};

struct DefaultObjectTag {};

// ContainableBaseClasses<> makes it easy to define types that live in multiple
// intrusive containers at once.
//
// If you didn't use this helper template, you would have to define multiple
// traits classes, each with their own node_state function, and then have
// multiple NodeState members in your class. This is noisy boilerplate, so
// instead you can just do something like the following:
//
// struct MyTag1 {};
// struct MyTag2 {};
// struct MyTag3 {};
//
// class MyClass
//     : public fbl::RefCounted<MyClass>,
//       public fbl::ContainableBaseClasses<
//           fbl::WAVLTreeContainable<fbl::RefPtr<MyClass>, MyTag1>,
//           fbl::WAVLTreeContainable<fbl::RefPtr<MyClass>, MyTag2>,
//           fbl::SinglyLinkedListable<MyClass*, MyTag3>,
//           [...]> { <your class definition> };
//
// Then when you create your container, you use the same tag type:
//
// fbl::TaggedWAVLTree<uint32_t, fbl::RefPtr<MyClass>, MyTag1> my_tree;
//
// The tag types themselves can be basically anything but I recommend you define
// your own empty structs to keep it simple and make it a type that you own.
//
// (Note for the curious: the tag types are necessary to solve the diamond
// problem, since your class ends up with multiple node_state_ members from the
// non-virtual multiple inheritance and the compiler needs to know which one you
// want.)
//
// When you inherit from this template, your class will also end up with a
// TagTypes member, which is just a std::tuple of your tag types, so that
// you can query these for metaprogramming purposes.
//
// You should also get relatively readable error messages for common error cases
// because of a few static_asserts; notably, you cannot:
// ++ Use any variation of unique_ptr as the PtrType here since that would defeat
//    its purpose.
// ++ Explicitly use the DefaultObjectTag that is used as the tag type when
//    the user does not specify one.
// ++ Pass the same tag type twice.
//
template <typename... BaseClasses>
struct ContainableBaseClasses;

template <>
struct ContainableBaseClasses<> {
  using ContainableTypes = std::tuple<>;
  using TagTypes = std::tuple<>;
};

template <template <typename, typename> class Containable, typename PtrType, typename TagType,
          typename... Rest>
struct ContainableBaseClasses<Containable<PtrType, TagType>, Rest...>
    : public Containable<PtrType, TagType>, public ContainableBaseClasses<Rest...> {
  static_assert(internal::ContainerPtrTraits<PtrType>::CanCopy || sizeof...(Rest) == 0,
                "You can't have a unique_ptr in multiple containers at once.");
  static_assert(!std::is_same_v<TagType, DefaultObjectTag>,
                "Do not use fbl::DefaultObjectTag when inheriting from "
                "fbl::ContainableBaseClasses; define your own instead.");
  static_assert((!std::is_same_v<TagType, typename Rest::TagType> && ...),
                "All tag types used with fbl::ContainableBaseClasses must be unique.");

  using ContainableTypes = decltype(
      std::tuple_cat(std::declval<std::tuple<Containable<PtrType, TagType>>>(),
                     std::declval<typename ContainableBaseClasses<Rest...>::ContainableTypes>()));
  using TagTypes =
      decltype(std::tuple_cat(std::declval<std::tuple<TagType>>(),
                              std::declval<typename ContainableBaseClasses<Rest...>::TagTypes>()));
};

namespace internal {
DECLARE_HAS_MEMBER_TYPE(has_tag_types, TagTypes);
}

// This is a free function because making it a member function presents complicated lookup issues
// since the specific Containable classes already have a non-template InContainer, and you'd need to
// say obj.template InContainer<TagType>(), which is super ugly.
template <typename TagType, typename Containable, size_t Index = 0,
          typename = std::enable_if_t<internal::has_tag_types_v<Containable>>>
bool InContainer(const Containable& c) {
  static_assert(Index < std::tuple_size_v<typename Containable::TagTypes>,
                "Containable is not a member of a container with the specified tag type.");

  using SpecificContainable = std::tuple_element_t<Index, typename Containable::ContainableTypes>;
  if constexpr (std::is_same_v<TagType, typename SpecificContainable::TagType>) {
    return InContainer(static_cast<const SpecificContainable&>(c));
  } else {
    return InContainer<TagType, Containable, Index + 1>(c);
  }
}

// Overload for specific containables so this function can be used generically in any situation,
// similarly to std::begin, std::end, std::size, etc.
template <typename TagType = DefaultObjectTag, typename Containable,
          typename = std::enable_if_t<!internal::has_tag_types_v<Containable>>>
bool InContainer(const Containable& c) {
  // It's okay to let people leave TagType as DefaultObjectTag because if there are multiple TagType
  // member typedefs, the compiler will complain. We just want to prevent people from passing a
  // nonsensical TagType parameter.
  static_assert(std::is_same_v<TagType, typename Containable::TagType> ||
                    std::is_same_v<TagType, DefaultObjectTag>,
                "Containable is not a member of a container with the specified tag type.");

  return c.InContainer();
}

// An enumeration which can be used as a template argument on list types to
// control the order of operation needed to compute the size of the list.  When
// set to SizeOrder::N, the list's size will not be maintained and there will be
// no valid size() method to call.  The only way to fetch the size of a list
// would be via |size_slow()|.  Alternatively, a user may specify
// SizeOrder::Constant.  In this case, the storage size of the list itself will
// grow by a size_t, and the size of the list will be maintained as elements are
// added and removed.
enum class SizeOrder { N, Constant };

}  // namespace fbl

namespace fbl::internal {

// DirectEraseUtils
//
// A utility class used by HashTable to implement an O(n) or O(k) direct erase
// operation depending on whether or not the HashTable's bucket type supports
// O(k) erase.
template <typename ContainerType, typename Enable = void>
struct DirectEraseUtils;

template <typename ContainerType>
struct DirectEraseUtils<
    ContainerType, std::enable_if_t<ContainerType::SupportsConstantOrderErase == false, void>> {
  using PtrTraits = typename ContainerType::PtrTraits;
  using PtrType = typename PtrTraits::PtrType;
  using ValueType = typename PtrTraits::ValueType;

  static PtrType erase(ContainerType& container, ValueType& obj) {
    return container.erase_if([&obj](const ValueType& other) -> bool { return &obj == &other; });
  }
};

template <typename ContainerType>
struct DirectEraseUtils<ContainerType,
                        std::enable_if_t<ContainerType::SupportsConstantOrderErase == true, void>> {
  using PtrTraits = typename ContainerType::PtrTraits;
  using PtrType = typename PtrTraits::PtrType;
  using ValueType = typename PtrTraits::ValueType;

  static PtrType erase(ContainerType& container, ValueType& obj) { return container.erase(obj); }
};

// KeyEraseUtils
//
// A utility class used by HashTable to implement an O(n) or O(k) erase-by-key
// operation depending on whether or not the HashTable's bucket type is
// associative or not.
template <typename ContainerType, typename KeyTraits, typename Enable = void>
struct KeyEraseUtils;

template <typename ContainerType, typename KeyTraits>
struct KeyEraseUtils<ContainerType, KeyTraits,
                     std::enable_if_t<ContainerType::IsAssociative == false, void>> {
  using PtrTraits = typename ContainerType::PtrTraits;
  using PtrType = typename PtrTraits::PtrType;
  using ValueType = typename PtrTraits::ValueType;

  template <typename KeyType>
  static PtrType erase(ContainerType& container, const KeyType& key) {
    return container.erase_if([key](const ValueType& other) -> bool {
      return KeyTraits::EqualTo(key, KeyTraits::GetKey(other));
    });
  }
};

template <typename ContainerType, typename KeyTraits>
struct KeyEraseUtils<ContainerType, KeyTraits,
                     std::enable_if_t<ContainerType::IsAssociative == true, void>> {
  using PtrTraits = typename ContainerType::PtrTraits;
  using PtrType = typename PtrTraits::PtrType;

  template <typename KeyType>
  static PtrType erase(ContainerType& container, const KeyType& key) {
    return container.erase(key);
  }
};

// Swaps two plain old data types with size no greater than 64 bits.
template <typename T, typename = std::enable_if_t<std::is_pod_v<T> && (sizeof(T) <= 8)>>
inline void Swap(T& a, T& b) noexcept {
  T tmp = a;
  a = b;
  b = tmp;
}

// Notes on container sentinels:
//
// Intrusive container implementations employ a slightly tricky pattern where
// sentinel values are used in place of nullptr in various places in the
// internal data structure in order to make some operations a bit easier.
// Generally speaking, a sentinel pointer is a pointer to a container with the
// sentinel bit set.  It is cast and stored in the container's data structure as
// a pointer to an element which the container contains, even though it is
// actually a slightly damaged pointer to the container itself.
//
// An example of where this is used is in the doubly linked list implementation.
// The final element in the list holds the container's sentinel value instead of
// nullptr or a pointer to the head of the list.  When an iterator hits the end
// of the list, it knows that it is at the end (because the sentinel bit is set)
// but can still get back to the list itself (by clearing the sentinel bit in
// the pointer) without needing to store an explicit pointer to the list itself.
//
// Care must be taken when using sentinel values.  They are *not* valid pointers
// and must never be dereferenced, recovered into an managed representation, or
// returned to a user.  In addition, it is essential that a legitimate pointer
// to a container never need to set the sentinel bit.  Currently, bit 0 is being
// used because it should never be possible to have a proper container instance
// which is odd-aligned.
constexpr uintptr_t kContainerSentinelBit = 1U;

// Create a sentinel pointer from a raw pointer, converting it to the specified
// type in the process.
template <typename T, typename U, typename = std::enable_if_t<std::is_pointer_v<T>>>
constexpr T make_sentinel(U* ptr) {
  return reinterpret_cast<T>(reinterpret_cast<uintptr_t>(ptr) | kContainerSentinelBit);
}

template <typename T, typename = std::enable_if_t<std::is_pointer_v<T>>>
constexpr T make_sentinel(decltype(nullptr)) {
  return reinterpret_cast<T>(kContainerSentinelBit);
}

// Turn a sentinel pointer back into a normal pointer, automatically
// re-interpreting its type in the process.
template <typename T, typename U, typename = std::enable_if_t<std::is_pointer_v<T>>>
constexpr T unmake_sentinel(U* sentinel) {
  return reinterpret_cast<T>(reinterpret_cast<uintptr_t>(sentinel) & ~kContainerSentinelBit);
}

// Test to see if a pointer is a sentinel pointer.
template <typename T>
constexpr bool is_sentinel_ptr(const T* ptr) {
  return (reinterpret_cast<uintptr_t>(ptr) & kContainerSentinelBit) != 0;
}

// Test to see if a pointer (which may be a sentinel) is valid.  Valid in this
// context means that the pointer is not null, and is not a sentinel.
template <typename T>
constexpr bool valid_sentinel_ptr(const T* ptr) {
  return ptr && !is_sentinel_ptr(ptr);
}

DECLARE_HAS_MEMBER_FN(has_node_state, node_state);

// SizeTracker is a partially specialized internal class used to track (or
// explicitly to not track) the size of Lists in the fbl:: containers.  Its
// behavior and size depends on the SizeOrder template parameter passed to it.
//
// Please note that to use this class, containers must (sadly) derive from it, they
// cannot simply encapsulate it.  The SizeOrder::N version of the tracker is
// nominally of 0 size, however 0 sized members of a struct/class are not allowed
// in C++.  Attempting to put a 0 sized member into a class results in at least
// 1 byte of size impact, which changes the size of the entire object.
//
// 0 sized base classes, however, are totally fine.  So, if we encapsulate a
// SizeTracker<SizeOrder::N>, then our container gets bigger for no reason, but
// if we derive from one, then our container stays the size that we expect it
// to.
//
// static_assert tests for this exist in the non-sized doubly and singly linked
// list tests.
template <SizeOrder>
class SizeTracker;

template <>
class SizeTracker<SizeOrder::N> {
 protected:
  constexpr SizeTracker() = default;
  ~SizeTracker() = default;

  // No copy, no move.
  SizeTracker(const SizeTracker&) = delete;
  SizeTracker& operator=(const SizeTracker&) = delete;
  SizeTracker(SizeTracker&& other) = delete;
  SizeTracker& operator=(SizeTracker&& other) = delete;

  // Inc, Dec, Reset, and swap operations are no-ops.  There is no count
  // accessor.  Anyone who attempts to access count has made a mistake.
  void IncSizeTracker(size_t) {}
  void DecSizeTracker(size_t) {}
  void ResetSizeTracker() {}
  void SwapSizeTracker(SizeTracker&) {}
};

template <>
class SizeTracker<SizeOrder::Constant> {
 protected:
  constexpr SizeTracker() = default;
  ~SizeTracker() = default;

  // No copy, no move.
  SizeTracker(const SizeTracker&) = delete;
  SizeTracker& operator=(const SizeTracker&) = delete;
  SizeTracker(SizeTracker&& other) = delete;
  SizeTracker& operator=(SizeTracker&& other) = delete;

  // Basic operations for manipulating the count storage.
  void IncSizeTracker(size_t amt) { size_tracker_count_ += amt; }
  void DecSizeTracker(size_t amt) { size_tracker_count_ -= amt; }
  void ResetSizeTracker() { size_tracker_count_ = 0; }
  void SwapSizeTracker(SizeTracker& other) {
    std::swap(size_tracker_count_, other.size_tracker_count_);
  }
  size_t SizeTrackerCount() const { return size_tracker_count_; }

 private:
  size_t size_tracker_count_ = 0;
};

}  // namespace fbl::internal

#endif  // FBL_INTRUSIVE_CONTAINER_UTILS_H_

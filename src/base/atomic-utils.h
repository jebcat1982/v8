// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ATOMIC_UTILS_H_
#define V8_ATOMIC_UTILS_H_

#include <limits.h>
#include <type_traits>

#include "src/base/atomicops.h"
#include "src/base/macros.h"

namespace v8 {
namespace base {

template <class T>
class AtomicNumber {
 public:
  AtomicNumber() : value_(0) {}
  explicit AtomicNumber(T initial) : value_(initial) {}

  // Returns the value after incrementing.
  V8_INLINE T Increment(T increment) {
    return static_cast<T>(base::Barrier_AtomicIncrement(
        &value_, static_cast<base::AtomicWord>(increment)));
  }

  // Returns the value after decrementing.
  V8_INLINE T Decrement(T decrement) {
    return static_cast<T>(base::Barrier_AtomicIncrement(
        &value_, -static_cast<base::AtomicWord>(decrement)));
  }

  V8_INLINE T Value() const {
    return static_cast<T>(base::Acquire_Load(&value_));
  }

  V8_INLINE void SetValue(T new_value) {
    base::Release_Store(&value_, static_cast<base::AtomicWord>(new_value));
  }

  V8_INLINE T operator=(T value) {
    SetValue(value);
    return value;
  }

  V8_INLINE T operator+=(T value) { return Increment(value); }
  V8_INLINE T operator-=(T value) { return Decrement(value); }

 private:
  STATIC_ASSERT(sizeof(T) <= sizeof(base::AtomicWord));

  base::AtomicWord value_;
};

// This type uses no barrier accessors to change atomic word. Be careful with
// data races.
template <typename T>
class NoBarrierAtomicValue {
 public:
  NoBarrierAtomicValue() : value_(0) {}

  explicit NoBarrierAtomicValue(T initial)
      : value_(cast_helper<T>::to_storage_type(initial)) {}

  static NoBarrierAtomicValue* FromAddress(void* address) {
    return reinterpret_cast<base::NoBarrierAtomicValue<T>*>(address);
  }

  V8_INLINE bool TrySetValue(T old_value, T new_value) {
    return base::Relaxed_CompareAndSwap(
               &value_, cast_helper<T>::to_storage_type(old_value),
               cast_helper<T>::to_storage_type(new_value)) ==
           cast_helper<T>::to_storage_type(old_value);
  }

  V8_INLINE T Value() const {
    return cast_helper<T>::to_return_type(base::Relaxed_Load(&value_));
  }

  V8_INLINE void SetValue(T new_value) {
    base::Relaxed_Store(&value_, cast_helper<T>::to_storage_type(new_value));
  }

 private:
  STATIC_ASSERT(sizeof(T) <= sizeof(base::AtomicWord));

  template <typename S>
  struct cast_helper {
    static base::AtomicWord to_storage_type(S value) {
      return static_cast<base::AtomicWord>(value);
    }
    static S to_return_type(base::AtomicWord value) {
      return static_cast<S>(value);
    }
  };

  template <typename S>
  struct cast_helper<S*> {
    static base::AtomicWord to_storage_type(S* value) {
      return reinterpret_cast<base::AtomicWord>(value);
    }
    static S* to_return_type(base::AtomicWord value) {
      return reinterpret_cast<S*>(value);
    }
  };

  base::AtomicWord value_;
};

// Flag using T atomically. Also accepts void* as T.
template <typename T>
class AtomicValue {
 public:
  AtomicValue() : value_(0) {}

  explicit AtomicValue(T initial)
      : value_(cast_helper<T>::to_storage_type(initial)) {}

  V8_INLINE T Value() const {
    return cast_helper<T>::to_return_type(base::Acquire_Load(&value_));
  }

  V8_INLINE bool TrySetValue(T old_value, T new_value) {
    return base::Release_CompareAndSwap(
               &value_, cast_helper<T>::to_storage_type(old_value),
               cast_helper<T>::to_storage_type(new_value)) ==
           cast_helper<T>::to_storage_type(old_value);
  }

  V8_INLINE void SetBits(T bits, T mask) {
    DCHECK_EQ(bits & ~mask, static_cast<T>(0));
    T old_value;
    T new_value;
    do {
      old_value = Value();
      new_value = (old_value & ~mask) | bits;
    } while (!TrySetValue(old_value, new_value));
  }

  V8_INLINE void SetBit(int bit) {
    SetBits(static_cast<T>(1) << bit, static_cast<T>(1) << bit);
  }

  V8_INLINE void ClearBit(int bit) { SetBits(0, 1 << bit); }

  V8_INLINE void SetValue(T new_value) {
    base::Release_Store(&value_, cast_helper<T>::to_storage_type(new_value));
  }

 private:
  STATIC_ASSERT(sizeof(T) <= sizeof(base::AtomicWord));

  template <typename S>
  struct cast_helper {
    static base::AtomicWord to_storage_type(S value) {
      return static_cast<base::AtomicWord>(value);
    }
    static S to_return_type(base::AtomicWord value) {
      return static_cast<S>(value);
    }
  };

  template <typename S>
  struct cast_helper<S*> {
    static base::AtomicWord to_storage_type(S* value) {
      return reinterpret_cast<base::AtomicWord>(value);
    }
    static S* to_return_type(base::AtomicWord value) {
      return reinterpret_cast<S*>(value);
    }
  };

  base::AtomicWord value_;
};


// See utils.h for EnumSet. Storage is always base::AtomicWord.
// Requirements on E:
// - No explicit values.
// - E::kLastValue defined to be the last actually used value.
//
// Example:
// enum E { kA, kB, kC, kLastValue = kC };
template <class E>
class AtomicEnumSet {
 public:
  explicit AtomicEnumSet(base::AtomicWord bits = 0) : bits_(bits) {}

  bool IsEmpty() const { return ToIntegral() == 0; }

  bool Contains(E element) const { return (ToIntegral() & Mask(element)) != 0; }
  bool ContainsAnyOf(const AtomicEnumSet& set) const {
    return (ToIntegral() & set.ToIntegral()) != 0;
  }

  void RemoveAll() { base::Release_Store(&bits_, 0); }

  bool operator==(const AtomicEnumSet& set) const {
    return ToIntegral() == set.ToIntegral();
  }

  bool operator!=(const AtomicEnumSet& set) const {
    return ToIntegral() != set.ToIntegral();
  }

  AtomicEnumSet<E> operator|(const AtomicEnumSet& set) const {
    return AtomicEnumSet<E>(ToIntegral() | set.ToIntegral());
  }

// The following operations modify the underlying storage.

#define ATOMIC_SET_WRITE(OP, NEW_VAL)                                     \
  do {                                                                    \
    base::AtomicWord old;                                                 \
    do {                                                                  \
      old = base::Acquire_Load(&bits_);                                   \
    } while (base::Release_CompareAndSwap(&bits_, old, old OP NEW_VAL) != \
             old);                                                        \
  } while (false)

  void Add(E element) { ATOMIC_SET_WRITE(|, Mask(element)); }

  void Add(const AtomicEnumSet& set) { ATOMIC_SET_WRITE(|, set.ToIntegral()); }

  void Remove(E element) { ATOMIC_SET_WRITE(&, ~Mask(element)); }

  void Remove(const AtomicEnumSet& set) {
    ATOMIC_SET_WRITE(&, ~set.ToIntegral());
  }

  void Intersect(const AtomicEnumSet& set) {
    ATOMIC_SET_WRITE(&, set.ToIntegral());
  }

#undef ATOMIC_SET_OP

 private:
  // Check whether there's enough storage to hold E.
  STATIC_ASSERT(E::kLastValue < (sizeof(base::AtomicWord) * CHAR_BIT));

  V8_INLINE base::AtomicWord ToIntegral() const {
    return base::Acquire_Load(&bits_);
  }

  V8_INLINE base::AtomicWord Mask(E element) const {
    return static_cast<base::AtomicWord>(1) << element;
  }

  base::AtomicWord bits_;
};

class AsAtomic32 {
 public:
  template <typename T>
  static T Acquire_Load(T* addr) {
    STATIC_ASSERT(sizeof(T) <= sizeof(base::Atomic32));
    return to_return_type<T>(base::Acquire_Load(to_storage_addr(addr)));
  }

  template <typename T>
  static T Relaxed_Load(T* addr) {
    STATIC_ASSERT(sizeof(T) <= sizeof(base::Atomic32));
    return to_return_type<T>(base::Relaxed_Load(to_storage_addr(addr)));
  }

  template <typename T>
  static void Release_Store(T* addr,
                            typename std::remove_reference<T>::type new_value) {
    STATIC_ASSERT(sizeof(T) <= sizeof(base::Atomic32));
    base::Release_Store(to_storage_addr(addr), to_storage_type(new_value));
  }

  template <typename T>
  static void Relaxed_Store(T* addr,
                            typename std::remove_reference<T>::type new_value) {
    STATIC_ASSERT(sizeof(T) <= sizeof(base::Atomic32));
    base::Relaxed_Store(to_storage_addr(addr), to_storage_type(new_value));
  }

  template <typename T>
  static T Release_CompareAndSwap(
      T* addr, typename std::remove_reference<T>::type old_value,
      typename std::remove_reference<T>::type new_value) {
    STATIC_ASSERT(sizeof(T) <= sizeof(base::Atomic32));
    return to_return_type<T>(base::Release_CompareAndSwap(
        to_storage_addr(addr), to_storage_type(old_value),
        to_storage_type(new_value)));
  }

  // Atomically sets bits selected by the mask to the given value.
  // Returns false if the bits are already set as needed.
  template <typename T>
  static bool SetBits(T* addr, T bits, T mask) {
    STATIC_ASSERT(sizeof(T) <= sizeof(base::Atomic32));
    DCHECK_EQ(bits & ~mask, static_cast<T>(0));
    T old_value;
    T new_value;
    do {
      old_value = Relaxed_Load(addr);
      if ((old_value & mask) == bits) return false;
      new_value = (old_value & ~mask) | bits;
    } while (Release_CompareAndSwap(addr, old_value, new_value) != old_value);
    return true;
  }

 private:
  template <typename T>
  static base::Atomic32 to_storage_type(T value) {
    return static_cast<base::Atomic32>(value);
  }
  template <typename T>
  static T to_return_type(base::Atomic32 value) {
    return static_cast<T>(value);
  }
  template <typename T>
  static base::Atomic32* to_storage_addr(T* value) {
    return reinterpret_cast<base::Atomic32*>(value);
  }
  template <typename T>
  static const base::Atomic32* to_storage_addr(const T* value) {
    return reinterpret_cast<const base::Atomic32*>(value);
  }
};

class AsAtomicWord {
 public:
  template <typename T>
  static T Acquire_Load(T* addr) {
    STATIC_ASSERT(sizeof(T) <= sizeof(base::AtomicWord));
    return to_return_type<T>(base::Acquire_Load(to_storage_addr(addr)));
  }

  template <typename T>
  static T Relaxed_Load(T* addr) {
    STATIC_ASSERT(sizeof(T) <= sizeof(base::AtomicWord));
    return to_return_type<T>(base::Relaxed_Load(to_storage_addr(addr)));
  }

  template <typename T>
  static void Release_Store(T* addr,
                            typename std::remove_reference<T>::type new_value) {
    STATIC_ASSERT(sizeof(T) <= sizeof(base::AtomicWord));
    base::Release_Store(to_storage_addr(addr), to_storage_type(new_value));
  }

  template <typename T>
  static void Relaxed_Store(T* addr,
                            typename std::remove_reference<T>::type new_value) {
    STATIC_ASSERT(sizeof(T) <= sizeof(base::AtomicWord));
    base::Relaxed_Store(to_storage_addr(addr), to_storage_type(new_value));
  }

  template <typename T>
  static T Release_CompareAndSwap(
      T* addr, typename std::remove_reference<T>::type old_value,
      typename std::remove_reference<T>::type new_value) {
    STATIC_ASSERT(sizeof(T) <= sizeof(base::AtomicWord));
    return to_return_type<T>(base::Release_CompareAndSwap(
        to_storage_addr(addr), to_storage_type(old_value),
        to_storage_type(new_value)));
  }

 private:
  template <typename T>
  static base::AtomicWord to_storage_type(T value) {
    return reinterpret_cast<base::AtomicWord>(value);
  }
  template <typename T>
  static T to_return_type(base::AtomicWord value) {
    return reinterpret_cast<T>(value);
  }
  template <typename T>
  static base::AtomicWord* to_storage_addr(T* value) {
    return reinterpret_cast<base::AtomicWord*>(value);
  }
  template <typename T>
  static const base::AtomicWord* to_storage_addr(const T* value) {
    return reinterpret_cast<const base::AtomicWord*>(value);
  }
};

}  // namespace base
}  // namespace v8

#endif  // #define V8_ATOMIC_UTILS_H_

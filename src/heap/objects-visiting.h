// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_VISITING_H_
#define V8_OBJECTS_VISITING_H_

#include "src/allocation.h"
#include "src/heap/embedder-tracing.h"
#include "src/heap/heap.h"
#include "src/heap/spaces.h"
#include "src/layout-descriptor.h"
#include "src/objects-body-descriptors.h"
#include "src/objects/string.h"

// This file provides base classes and auxiliary methods for defining
// static object visitors used during GC.
// Visiting HeapObject body with a normal ObjectVisitor requires performing
// two switches on object's instance type to determine object size and layout
// and one or more virtual method calls on visitor itself.
// Static visitor is different: it provides a dispatch table which contains
// pointers to specialized visit functions. Each map has the visitor_id
// field which contains an index of specialized visitor to use.

namespace v8 {
namespace internal {

#define VISITOR_ID_LIST(V) \
  V(AllocationSite)        \
  V(ByteArray)             \
  V(BytecodeArray)         \
  V(Cell)                  \
  V(Code)                  \
  V(ConsString)            \
  V(DataObject)            \
  V(FixedArray)            \
  V(FixedDoubleArray)      \
  V(FixedFloat64Array)     \
  V(FixedTypedArrayBase)   \
  V(FreeSpace)             \
  V(JSApiObject)           \
  V(JSArrayBuffer)         \
  V(JSFunction)            \
  V(JSObject)              \
  V(JSObjectFast)          \
  V(JSRegExp)              \
  V(JSWeakCollection)      \
  V(Map)                   \
  V(NativeContext)         \
  V(Oddball)               \
  V(PropertyCell)          \
  V(SeqOneByteString)      \
  V(SeqTwoByteString)      \
  V(SharedFunctionInfo)    \
  V(ShortcutCandidate)     \
  V(SlicedString)          \
  V(SmallOrderedHashMap)   \
  V(SmallOrderedHashSet)   \
  V(Struct)                \
  V(Symbol)                \
  V(ThinString)            \
  V(TransitionArray)       \
  V(WeakCell)

// For data objects, JS objects and structs along with generic visitor which
// can visit object of any size we provide visitors specialized by
// object size in words.
// Ids of specialized visitors are declared in a linear order (without
// holes) starting from the id of visitor specialized for 2 words objects
// (base visitor id) and ending with the id of generic visitor.
// Method GetVisitorIdForSize depends on this ordering to calculate visitor
// id of specialized visitor from given instance size, base visitor id and
// generic visitor's id.
enum VisitorId {
#define VISITOR_ID_ENUM_DECL(id) kVisit##id,
  VISITOR_ID_LIST(VISITOR_ID_ENUM_DECL)
#undef VISITOR_ID_ENUM_DECL
      kVisitorIdCount
};

// Base class for all static visitors.
class StaticVisitorBase : public AllStatic {
 public:
  // Visitor ID should fit in one byte.
  STATIC_ASSERT(kVisitorIdCount <= 256);

  // Determine which specialized visitor should be used for given instance type
  // and instance type.
  static VisitorId GetVisitorId(int instance_type, int instance_size,
                                bool has_unboxed_fields);

  // Determine which specialized visitor should be used for given map.
  static VisitorId GetVisitorId(Map* map);
};


template <typename Callback>
class VisitorDispatchTable {
 public:
  void CopyFrom(VisitorDispatchTable* other) {
    // We are not using memcpy to guarantee that during update
    // every element of callbacks_ array will remain correct
    // pointer (memcpy might be implemented as a byte copying loop).
    for (int i = 0; i < kVisitorIdCount; i++) {
      base::Relaxed_Store(&callbacks_[i], other->callbacks_[i]);
    }
  }

  inline Callback GetVisitor(Map* map);

  inline Callback GetVisitorById(VisitorId id) {
    return reinterpret_cast<Callback>(callbacks_[id]);
  }

  void Register(VisitorId id, Callback callback) {
    DCHECK(id < kVisitorIdCount);  // id is unsigned.
    callbacks_[id] = reinterpret_cast<base::AtomicWord>(callback);
  }

 private:
  base::AtomicWord callbacks_[kVisitorIdCount];
};


template <typename StaticVisitor, typename BodyDescriptor, typename ReturnType>
class FlexibleBodyVisitor : public AllStatic {
 public:
  INLINE(static ReturnType Visit(Map* map, HeapObject* object)) {
    int object_size = BodyDescriptor::SizeOf(map, object);
    BodyDescriptor::template IterateBody<StaticVisitor>(object, object_size);
    return static_cast<ReturnType>(object_size);
  }
};


template <typename StaticVisitor, typename BodyDescriptor, typename ReturnType>
class FixedBodyVisitor : public AllStatic {
 public:
  INLINE(static ReturnType Visit(Map* map, HeapObject* object)) {
    BodyDescriptor::template IterateBody<StaticVisitor>(object);
    return static_cast<ReturnType>(BodyDescriptor::kSize);
  }
};


// Base class for visitors used for a linear new space iteration.
// IterateBody returns size of visited object.
// Certain types of objects (i.e. Code objects) are not handled
// by dispatch table of this visitor because they cannot appear
// in the new space.
//
// This class is intended to be used in the following way:
//
//   class SomeVisitor : public StaticNewSpaceVisitor<SomeVisitor> {
//     ...
//   }
//
// This is an example of Curiously recurring template pattern
// (see http://en.wikipedia.org/wiki/Curiously_recurring_template_pattern).
// We use CRTP to guarantee aggressive compile time optimizations (i.e.
// inlining and specialization of StaticVisitor::VisitPointers methods).
template <typename StaticVisitor>
class StaticNewSpaceVisitor : public StaticVisitorBase {
 public:
  static void Initialize();

  INLINE(static int IterateBody(Map* map, HeapObject* obj)) {
    return table_.GetVisitor(map)(map, obj);
  }

  INLINE(static void VisitPointers(Heap* heap, HeapObject* object,
                                   Object** start, Object** end)) {
    for (Object** p = start; p < end; p++) {
      StaticVisitor::VisitPointer(heap, object, p);
    }
  }

  inline static void VisitCodeEntry(Heap* heap, HeapObject* object,
                                    Address entry_address) {
    // Code is not in new space.
  }

 private:
  inline static int UnreachableVisitor(Map* map, HeapObject* object) {
    UNREACHABLE();
  }

  INLINE(static int VisitByteArray(Map* map, HeapObject* object)) {
    return reinterpret_cast<ByteArray*>(object)->ByteArraySize();
  }

  INLINE(static int VisitFixedDoubleArray(Map* map, HeapObject* object)) {
    int length = reinterpret_cast<FixedDoubleArray*>(object)->length();
    return FixedDoubleArray::SizeFor(length);
  }

  INLINE(static int VisitSeqOneByteString(Map* map, HeapObject* object)) {
    return SeqOneByteString::cast(object)
        ->SeqOneByteStringSize(map->instance_type());
  }

  INLINE(static int VisitSeqTwoByteString(Map* map, HeapObject* object)) {
    return SeqTwoByteString::cast(object)
        ->SeqTwoByteStringSize(map->instance_type());
  }

  INLINE(static int VisitFreeSpace(Map* map, HeapObject* object)) {
    return FreeSpace::cast(object)->size();
  }

  class DataObjectVisitor {
   public:
    template <int object_size>
    static inline int VisitSpecialized(Map* map, HeapObject* object) {
      return object_size;
    }

    INLINE(static int Visit(Map* map, HeapObject* object)) {
      return map->instance_size();
    }
  };

  typedef FlexibleBodyVisitor<StaticVisitor, StructBodyDescriptor, int>
      StructVisitor;

  typedef FlexibleBodyVisitor<StaticVisitor, JSObject::BodyDescriptor, int>
      JSObjectVisitor;

  // Visitor for JSObjects without unboxed double fields.
  typedef FlexibleBodyVisitor<StaticVisitor, JSObject::FastBodyDescriptor, int>
      JSObjectFastVisitor;

  typedef int (*Callback)(Map* map, HeapObject* object);

  static VisitorDispatchTable<Callback> table_;
};


template <typename StaticVisitor>
VisitorDispatchTable<typename StaticNewSpaceVisitor<StaticVisitor>::Callback>
    StaticNewSpaceVisitor<StaticVisitor>::table_;


// Base class for visitors used to transitively mark the entire heap.
// IterateBody returns nothing.
// Certain types of objects might not be handled by this base class and
// no visitor function is registered by the generic initialization. A
// specialized visitor function needs to be provided by the inheriting
// class itself for those cases.
//
// This class is intended to be used in the following way:
//
//   class SomeVisitor : public StaticMarkingVisitor<SomeVisitor> {
//     ...
//   }
//
// This is an example of Curiously recurring template pattern.
template <typename StaticVisitor>
class StaticMarkingVisitor : public StaticVisitorBase {
 public:
  static void Initialize();

  INLINE(static void IterateBody(Map* map, HeapObject* obj)) {
    table_.GetVisitor(map)(map, obj);
  }

  INLINE(static void VisitWeakCell(Map* map, HeapObject* object));
  INLINE(static void VisitTransitionArray(Map* map, HeapObject* object));
  INLINE(static void VisitCodeEntry(Heap* heap, HeapObject* object,
                                    Address entry_address));
  INLINE(static void VisitEmbeddedPointer(Heap* heap, RelocInfo* rinfo));
  INLINE(static void VisitCell(Heap* heap, RelocInfo* rinfo));
  INLINE(static void VisitDebugTarget(Heap* heap, RelocInfo* rinfo));
  INLINE(static void VisitCodeTarget(Heap* heap, RelocInfo* rinfo));
  INLINE(static void VisitCodeAgeSequence(Heap* heap, RelocInfo* rinfo));
  INLINE(static void VisitExternalReference(RelocInfo* rinfo)) {}
  INLINE(static void VisitInternalReference(RelocInfo* rinfo)) {}
  INLINE(static void VisitRuntimeEntry(RelocInfo* rinfo)) {}
  // Skip the weak next code link in a code object.
  INLINE(static void VisitNextCodeLink(Heap* heap, Object** slot)) {}

 protected:
  INLINE(static void VisitMap(Map* map, HeapObject* object));
  INLINE(static void VisitCode(Map* map, HeapObject* object));
  INLINE(static void VisitBytecodeArray(Map* map, HeapObject* object));
  INLINE(static void VisitSharedFunctionInfo(Map* map, HeapObject* object));
  INLINE(static void VisitWeakCollection(Map* map, HeapObject* object));
  INLINE(static void VisitJSFunction(Map* map, HeapObject* object));
  INLINE(static void VisitNativeContext(Map* map, HeapObject* object));

  // Mark pointers in a Map treating some elements of the descriptor array weak.
  static void MarkMapContents(Heap* heap, Map* map);

  class DataObjectVisitor {
   public:
    template <int size>
    static inline void VisitSpecialized(Map* map, HeapObject* object) {}

    INLINE(static void Visit(Map* map, HeapObject* object)) {}
  };

  typedef FlexibleBodyVisitor<StaticVisitor, FixedArray::BodyDescriptor, void>
      FixedArrayVisitor;

  typedef FlexibleBodyVisitor<StaticVisitor, JSObject::FastBodyDescriptor, void>
      JSObjectFastVisitor;
  typedef FlexibleBodyVisitor<StaticVisitor, JSObject::BodyDescriptor, void>
      JSObjectVisitor;

  class JSApiObjectVisitor : AllStatic {
   public:
    INLINE(static void Visit(Map* map, HeapObject* object)) {
      TracePossibleWrapper(object);
      JSObjectVisitor::Visit(map, object);
    }

   private:
    INLINE(static void TracePossibleWrapper(HeapObject* object)) {
      if (object->GetHeap()->local_embedder_heap_tracer()->InUse()) {
        DCHECK(object->IsJSObject());
        object->GetHeap()->TracePossibleWrapper(JSObject::cast(object));
      }
    }
  };

  typedef FlexibleBodyVisitor<StaticVisitor, StructBodyDescriptor, void>
      StructObjectVisitor;

  typedef void (*Callback)(Map* map, HeapObject* object);

  static VisitorDispatchTable<Callback> table_;
};


template <typename StaticVisitor>
VisitorDispatchTable<typename StaticMarkingVisitor<StaticVisitor>::Callback>
    StaticMarkingVisitor<StaticVisitor>::table_;

#define TYPED_VISITOR_ID_LIST(V) \
  V(AllocationSite)              \
  V(ByteArray)                   \
  V(BytecodeArray)               \
  V(Cell)                        \
  V(Code)                        \
  V(ConsString)                  \
  V(FixedArray)                  \
  V(FixedDoubleArray)            \
  V(FixedFloat64Array)           \
  V(FixedTypedArrayBase)         \
  V(JSArrayBuffer)               \
  V(JSFunction)                  \
  V(JSObject)                    \
  V(JSRegExp)                    \
  V(JSWeakCollection)            \
  V(Map)                         \
  V(Oddball)                     \
  V(PropertyCell)                \
  V(SeqOneByteString)            \
  V(SeqTwoByteString)            \
  V(SharedFunctionInfo)          \
  V(SlicedString)                \
  V(SmallOrderedHashMap)         \
  V(SmallOrderedHashSet)         \
  V(Symbol)                      \
  V(ThinString)                  \
  V(TransitionArray)             \
  V(WeakCell)

// The base class for visitors that need to dispatch on object type.
// It is similar to StaticVisitor except it uses virtual dispatch
// instead of static dispatch table. The default behavour of all
// visit functions is to iterate body of the given object using
// the BodyDescriptor of the object.
//
// The visit functions return the size of the object cast to ResultType.
//
// This class is intended to be used in the following way:
//
//   class SomeVisitor : public HeapVisitor<ResultType, SomeVisitor> {
//     ...
//   }
//
// This is an example of Curiously recurring template pattern.
// TODO(ulan): replace static visitors with the HeapVisitor.
template <typename ResultType, typename ConcreteVisitor>
class HeapVisitor : public ObjectVisitor {
 public:
  ResultType Visit(HeapObject* object);
  ResultType Visit(Map* map, HeapObject* object);

 protected:
  // A guard predicate for visiting the object.
  // If it returns false then the default implementations of the Visit*
  // functions bailout from iterating the object pointers.
  virtual bool ShouldVisit(HeapObject* object);
  // A callback for visiting the map pointer in the object header.
  virtual void VisitMapPointer(HeapObject* host, HeapObject** map);

#define VISIT(type) virtual ResultType Visit##type(Map* map, type* object);
  TYPED_VISITOR_ID_LIST(VISIT)
#undef VISIT
  virtual ResultType VisitShortcutCandidate(Map* map, ConsString* object);
  virtual ResultType VisitNativeContext(Map* map, Context* object);
  virtual ResultType VisitDataObject(Map* map, HeapObject* object);
  virtual ResultType VisitJSObjectFast(Map* map, JSObject* object);
  virtual ResultType VisitJSApiObject(Map* map, JSObject* object);
  virtual ResultType VisitStruct(Map* map, HeapObject* object);
  virtual ResultType VisitFreeSpace(Map* map, FreeSpace* object);
};

class WeakObjectRetainer;

// A weak list is single linked list where each element has a weak pointer to
// the next element. Given the head of the list, this function removes dead
// elements from the list and if requested records slots for next-element
// pointers. The template parameter T is a WeakListVisitor that defines how to
// access the next-element pointers.
template <class T>
Object* VisitWeakList(Heap* heap, Object* list, WeakObjectRetainer* retainer);
}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_VISITING_H_

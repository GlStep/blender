/* SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup DNA
 *
 * Group generic defines for all DNA headers may use in this file.
 */

#pragma once

/* makesdna ignores */
#ifdef DNA_DEPRECATED_ALLOW
/* allow use of deprecated items */
#  define DNA_DEPRECATED
#else
#  ifndef DNA_DEPRECATED
#    ifdef __GNUC__
#      define DNA_DEPRECATED __attribute__((deprecated))
#    else
/* TODO: MSVC & others. */
#      define DNA_DEPRECATED
#    endif
#  endif
#endif

#ifdef __GNUC__
#  define DNA_PRIVATE_ATTR __attribute__((deprecated))
#else
#  define DNA_PRIVATE_ATTR
#endif

/* poison pragma */
#ifdef DNA_DEPRECATED_ALLOW
#  define DNA_DEPRECATED_GCC_POISON 0
#else
/* enable the pragma if we can */
#  ifdef __GNUC__
#    define DNA_DEPRECATED_GCC_POISON 1
#  else
#    define DNA_DEPRECATED_GCC_POISON 0
#  endif
#endif

/* hrmf, we need a better include then this */
#include "../blenlib/BLI_sys_types.h" /* needed for int64_t only! */

/* non-id name variables should use this length */
#define MAX_NAME 64

/* #DNA_DEFINE_CXX_METHODS is used to define C++ methods which are needed for proper/safe resource
 * management, making unsafe (from an ownership perspective: i.e. pointers which sometimes needs to
 * be set to nullptr on copy, sometimes needs to be dupalloc-ed) operations explicit, and taking
 * care of compiler specific warnings when dealing with members marked with DNA_DEPRECATED.
 *
 * The `class_name` argument is to match the structure name the macro is used from.
 *
 * Typical usage example:
 *
 *   typedef struct Object {
 *     DNA_DEFINE_CXX_METHODS(Object)
 *   } Object;
 */
#ifndef __cplusplus
#  define DNA_DEFINE_CXX_METHODS(class_name)
#else

/* Forward-declared here since there is no simple header file to be pulled for this functionality.
 * Avoids pulling `string.h` from this header to get access to #memcpy. */
extern "C" void _DNA_internal_memcpy(void *dst, const void *src, size_t size);

namespace blender::dna::internal {

template<class T> class ShallowDataConstRef {
 public:
  constexpr explicit ShallowDataConstRef(const T &ref) : ref_(ref)
  {
  }

  inline const T *get_pointer() const
  {
    return &ref_;
  }

 private:
  const T &ref_;
};

}  // namespace blender::dna::internal

#  define DNA_DEFINE_CXX_METHODS(class_name) \
    class_name() = default; \
    ~class_name() = default; \
    /* Delete copy and assignment, which are not safe for resource ownership. */ \
    class_name(const class_name &other) = delete; \
    class_name(class_name &&other) noexcept = delete; \
    class_name &operator=(const class_name &other) = delete; \
    class_name &operator=(class_name &&other) = delete; \
    /* Support for shallow copy. */ \
    class_name(const blender::dna::internal::ShallowDataConstRef<class_name> ref) \
    { \
      _DNA_internal_memcpy(this, ref.get_pointer(), sizeof(class_name)); \
    } \
    class_name &operator=(const blender::dna::internal::ShallowDataConstRef<class_name> ref) \
    { \
      if (this != ref.get_pointer()) { \
        _DNA_internal_memcpy(this, ref.get_pointer(), sizeof(class_name)); \
      } \
      return *this; \
    }

namespace blender::dna {

/* Creates shallow copy of the given object.
 * The entire object is copied as-is using memory copy.
 *
 * Typical usage:
 *   Object temp_object = blender::dna::shallow_copy(*input_object);
 *
 * From the implementation detail go via copy constructor/assign operator defined in the structure.
 */
template<class T>
[[nodiscard]] inline internal::ShallowDataConstRef<T> shallow_copy(const T &other)
{
  return internal::ShallowDataConstRef(other);
}

}  // namespace blender::dna

#endif

#include "Runtime.PPCEABI.H/NMWException.h"
#include <stddef.h>

// Constructors and destructors called by the array helpers. The second argument is 1 for
// "construct the complete object" and -1 for "destroy it without freeing the storage".
typedef void (*ArrayElementFunc)(void* object, short mode);

// operator new[] storage starts with a 16-byte header holding the element size and count
#define NEW_ARRAY_HEADER_SIZE 0x10

// Lives on the stack while the elements of an array are being constructed. If a constructor
// throws, unwinding runs the destructor, which destroys the elements built so far in reverse.
class ArrayConstructGuard {
public:
    ArrayConstructGuard(void* start, size_t size, size_t count, ArrayElementFunc dtor) {
        mStart = start;
        mSize = size;
        mCount = count;
        mDtor = dtor;
        mBuilt = 0;
    }

    ~ArrayConstructGuard() {
        if (mBuilt < mCount && mDtor != NULL) {
            char* object = (char*)mStart + mSize * mBuilt;
            while (mBuilt != 0) {
                object -= mSize;
                mDtor(object, -1);
                mBuilt--;
            }
        }
    }

    void* mStart;
    size_t mSize;
    size_t mCount;
    ArrayElementFunc mDtor;
    size_t mBuilt; // number of elements constructed so far
};

// Fills the storage returned by operator new[]: records size and count in the header, then
// constructs each element. Returns the address of the first element (NULL if block is NULL).
extern "C" void* __construct_new_array(void* block, ArrayElementFunc ctor, ArrayElementFunc dtor, size_t size,
                                       size_t count) {
    char* elements = (char*)block;

    if (elements != NULL) {
        ((size_t*)elements)[0] = size;
        ((size_t*)elements)[1] = count;
        elements += NEW_ARRAY_HEADER_SIZE;

        if (ctor != NULL) {
            ArrayConstructGuard guard(elements, size, count, dtor);
            char* object = elements;

            for (; guard.mBuilt < count; guard.mBuilt++) {
                ctor(object, 1);
                object += size;
            }
        }
    }

    return elements;
}

// Constructs count elements of an array that is not a new[] allocation (locals, members)
extern "C" void __construct_array(void* start, ArrayElementFunc ctor, ArrayElementFunc dtor, size_t size,
                                  size_t count) {
    ArrayConstructGuard guard(start, size, count, dtor);
    char* object = (char*)start;

    for (; guard.mBuilt < count; guard.mBuilt++) {
        ctor(object, 1);
        object += size;
    }
}

// Destroys count elements of an array, last element first
extern "C" void __destroy_arr(void* start, ArrayElementFunc dtor, size_t size, size_t count) {
    char* object = (char*)start + size * count;

    while (count != 0) {
        object -= size;
        dtor(object, -1);
        count--;
    }
}

// Implements delete[] for arrays of class objects: destroys the elements (last first), using
// the size and count stored in the header, then frees the whole allocation
extern "C" void __destroy_new_array(void* elements, ArrayElementFunc dtor) {
    if (elements != NULL) {
        if (dtor != NULL) {
            size_t i;
            size_t count;
            size_t size;
            char* object;

            size = ((size_t*)((char*)elements - NEW_ARRAY_HEADER_SIZE))[0];
            count = ((size_t*)((char*)elements - NEW_ARRAY_HEADER_SIZE))[1];
            object = (char*)elements + size * count;

            for (i = 0; i < count; i++) {
                object -= size;
                dtor(object, -1);
            }
        }

        operator delete[]((char*)elements - NEW_ARRAY_HEADER_SIZE);
    }
}

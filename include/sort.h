// Bubble sort is an algorithm that sorts an array from the lowest value to the highest value

#ifndef SORT_H
#define SORT_H

#include <stddef.h>
#include <strings.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <immintrin.h> //<--- COMMENT THIS INCLUDE IF YOU ARE CROSS COMPILING TO RISC-V! 

#define SORT_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static inline int compare_uints(const void *a, const void *b) {
 
   unsigned int x = *(const unsigned int *)a;
   unsigned int y = *(const unsigned int *)b;
   return (x > y) - (x < y);
}

static inline void sort_bubble_int(int* array, size_t n) {

   if (array == NULL || n < 2) {
       return;
   }

   for (size_t i = 0; i + 1 < n; ++i) {
	int swapped = 0;
	for (size_t j = 0; j + 1 < n - i; ++j) {
	    if (array[j] > array[j+1]) {
		int temp = array[j];
		array[j] = array[j+1];
		array[j+1] = temp;
		swapped = 1;
	    }
	}

	if (!swapped) {
	    break;
	}
   }
}

static inline int* sort_bubble_int_wrapper(int* array, size_t n) {

    if (array == NULL || n < 2) {
        return array;
    }

   sort_bubble_int(array, n);
   return array;
}

static inline void sort_bubble_char(char* array, size_t n) {

   if (array == NULL || n < 2) {
       return;
   }

   for (size_t i = 0; i + 1 < n; ++i) {
	int swapped = 0;
	for (size_t j = 0; j + 1 < n - i; ++j) {
	     if (array[j] > array[j+1]) {
	         char temp = array[j];
		 array[j] = array[j+1];
		 array[j+1] = temp;
                 swapped = 1;
	     }
	}
	
	if (!swapped) {
	    break;
	}
    }
}

static inline void sort_bubble_strs(char *array[], size_t n) {

   if (array == NULL || n < 2) {
       return;
   }

   for (size_t i = 0; i + 1 < n; ++i) {
	int swapped = 0;
        for (size_t j = 0; j + 1 < n - i; ++j) {
	     const char *a = array[j];
	     const char *b = array[j+1];

	     if (strcasecmp(a, b) > 0) {
		 char *tmp = array[j];
		 array[j] = array[j+1];
		 array[j+1] = tmp;
		 swapped = 1;
	     }
	}

	if (!swapped) {
	    break;
	}
   }
}

static inline void sort_bubble_float(float* array, size_t n) {

   if (array == NULL || n < 2) {
       return;
   }

   for (size_t i = 0; i + 1 < n; ++i) {
	int swapped = 0;
	for (size_t j = 0; j + 1 < n - i; ++j) {
	     if (array[j] > array[j+1]) {
		 float temp = array[j];
		 array[j] = array[j+1];
		 array[j+1] = temp;
		 swapped = 1;
	     }
	}

    	if (!swapped) {
	    break;
	}
   }
}

static inline void sort_bubble_bool(bool* array, size_t n) {

   if (array == NULL || n < 2) {
       return;
   }

   for (size_t i = 0; i + 1 < n; ++i) {
	int swapped = 0;
	for (size_t j = 0; j + 1 < n - i; ++j) {
	     if (array[j] > array[j + 1]) {
		 bool temp = array[j];
		 array[j] = array[j + 1];
		 array[j + 1] = temp;
		 swapped = 1;
	     }
	}

	if (!swapped) {
	    break;
	}
   }
}

static inline void sort_select_int(int* array, size_t n) {

   if (array == NULL || n < 2) {
       return;
   }

   for (size_t i = 0; i < n; i++) {
	int min_index = i;
	for (size_t j = i+1; j < n; j++) {
	     if (array[j] < array[min_index]) {
		 min_index = j;
	     }
	}

	int temp = array[i];
	array[i] = array[min_index];
	array[min_index] = temp;
   }
}

static inline void sort_select_float(float* array, size_t n) {

   if (array == NULL || n < 2) {
       return;
   }

   for (size_t i = 0; i < n; i++) {
	int min_index = i;
	for (size_t j = i+1; j < n; j++) {
	     if (array[j] < array[min_index]) {
	         min_index = j;
	     }
	}

        float temp = array[i];
        array[i] = array[min_index];
	array[min_index] = temp;
   }
}

static inline void sort_insert_int(int* array, size_t n) {

   if (array == NULL || n < 2) {
       return;
   }

   for (size_t i = 1; i < n; i++) {
	int insertIndex = i;
	int currentValue = array[i];
	int j = i - 1;

	while (j >= 0 && array[j] > currentValue) {
	    array[j + 1] = array[j];
	    insertIndex = j;
	    j--;
	}
	array[insertIndex] = currentValue;
   }
}

static inline int partition_int(int *array, int low, int high);
static inline int partition_float(float *array, int low, int high);

static inline int partition_int(int *array, int low, int high) {                                                     
                                                                                                                 
   int pivot = array[high];                                                                                      
   int i = low - 1;                                                                                              
                                                                                                                 
   for (int j = low; j < high; j++) {                                                                            
        if (array[j] <= pivot) {                                                                                 
            i++;                                                                                                 
            int temp = array[i];                                                                                 
            array[i] = array[j];                                                                                 
            array[j] = temp;                                                                                     
        }                                                                                                        
   }                                                                                                             
                                                                                                                 
   int temp = array[i + 1];                                                                                      
   array[i + 1] = array[high];                                                                                   
   array[high] = temp;                                                                                           
   return i + 1;                                                                                                 
}

static inline int partition_float(float *array, int low, int high) {

   float pivot = array[high];
   int i = low - 1;

   for (int j = low; j < high; j++) {
	if (array[j] <= pivot) {
	    i++;
	    float temp = array[i];
 	    array[i] = array[j];
	    array[j] = temp;
	}
   }

   float temp = array[i + 1];
   array[i + 1] = array[high];
   array[high] = temp;
   return i + 1;
}

static inline void sort_quick_int_range(int *array, int low, int high) {

    if (low < high) {
        int p = partition_int(array, low, high);
        sort_quick_int_range(array, low, p - 1);
        sort_quick_int_range(array, p + 1, high);
    }
}

static inline void sort_quick_float_range(float *array, int low, int high) {

    if (low < high) {
	int pf = partition_float(array, low, high);
	sort_quick_float_range(array, low, pf - 1);
	sort_quick_float_range(array, pf + 1, high);
    }
}

static inline void sort_quick_int(int *array, size_t n) {

   if (array == NULL || n < 2) {
       return;
   }

   sort_quick_int_range(array, 0, (int)n - 1);
}

static inline void sort_quick_float(float *array, size_t n) {

   if (array == NULL || n < 2) {
       return;
   }

   sort_quick_float_range(array, 0, (int)n - 1);
}

static inline void sort_count_int(int* array, size_t n) {

   if (array == NULL || n < 2) {
       return;
   }

   int max_value = array[0];
   for (int i = 1; i < n; i++) {
        if (array[i] > max_value) {
	    max_value = array[i];
	}
   }

   int count[max_value + 1];
   for (int i = 0; i <= max_value; i++) {
	count[i] = 0;
   }

   for (int i = 0; i < n; i++) {
	count[array[i]]++;
   }

   int index = 0;
   for (int i = 0; i <= max_value; i++) {
	while (count[i] > 0) {
	    array[index++] = i;
            count[i]--;
	}
   }
}

static inline void sort_radix_int(int* array, size_t n) {

    if (array == NULL || n < 2) {
        return;
    }

    int max_value = array[0];
    for (size_t i = 1; i < n; ++i) {
        if (array[i] > max_value) {
            max_value = array[i];
        }
    }

    /* This implementation assumes non-negative integers. */
    if (max_value < 0) {
        return;
    }

    int* output = (int*)malloc(sizeof(int) * n);
    if (output == NULL) {
        return;
    }

    for (int exp = 1; max_value / exp > 0; exp *= 10) {
        int count[10] = {0};

        for (size_t i = 0; i < n; ++i) {
            int digit = (array[i] / exp) % 10;
            count[digit]++;
        }

        for (int i = 1; i < 10; ++i) {
            count[i] += count[i - 1];
        }

        for (size_t i = n; i > 0; --i) {
            int value = array[i - 1];
            int digit = (value / exp) % 10;
            output[count[digit] - 1] = value;
            count[digit]--;
        }

        for (size_t i = 0; i < n; ++i) {
            array[i] = output[i];
        }
    }

    free(output);
}

static inline void merge_int(int *array, int start, int mid, int end);
static inline void merge_float(float *array, int start, int mid, int end);

static inline void sort_merge_int_range(int *array, int start, int end) {
    if (start < end) {
        int mid = start + (end - start) / 2;
        sort_merge_int_range(array, start, mid);
        sort_merge_int_range(array, mid + 1, end);
        merge_int(array, start, mid, end);
    }
}

static inline void sort_merge_float_range(float *array, int start, int end) {
    if (start < end) {
        int mid = start + (end - start) / 2;
        sort_merge_float_range(array, start, mid);
        sort_merge_float_range(array, mid + 1, end);
        merge_float(array, start, mid, end);
    }
}

static inline void sort_merge_int(int *array, size_t n) {
    if (array == NULL || n < 2) return;
    sort_merge_int_range(array, 0, (int)n - 1);
}

static inline void sort_merge_float(float *array, size_t n) {
    if (array == NULL || n < 2) return;
    sort_merge_float_range(array, 0, (int)n - 1);
}

static inline void merge_int(int *array, int start, int mid, int end) {
    int n1 = mid - start + 1;
    int n2 = end - mid;

    int left[n1], right[n2];

    for (int i = 0; i < n1; ++i) left[i] = array[start + i];
    for (int j = 0; j < n2; ++j) right[j] = array[mid + 1 + j];

    int i = 0, j = 0, k = start;
    while (i < n1 && j < n2) {
        if (left[i] <= right[j]) array[k++] = left[i++];
        else array[k++] = right[j++];
    }
    while (i < n1) array[k++] = left[i++];
    while (j < n2) array[k++] = right[j++];
}

static inline void merge_float(float *array, int start, int mid, int end) {
    int n1 = mid - start + 1;
    int n2 = end - mid;

    float left[n1], right[n2];

    for (int i = 0; i < n1; ++i) left[i] = array[start + i];
    for (int j = 0; j < n2; ++j) right[j] = array[mid + 1 + j];

    int i = 0, j = 0, k = start;
    while (i < n1 && j < n2) {
        if (left[i] <= right[j]) array[k++] = left[i++];
        else array[k++] = right[j++];
    }
    while (i < n1) array[k++] = left[i++];
    while (j < n2) array[k++] = right[j++];
}

static inline int linear_search(int *array, size_t n, int targetVal);
static inline int binary_search(int *array, size_t n, int targetVal);

static inline int linear_search(int *array, size_t n, int targetVal) {

   for (size_t i = 0; i < n; i++) {
	if (array[i] == targetVal) {
	    return i;
	}
   }
   return -1;
}

static inline int binary_search(int *array, size_t n, int targetVal) {

   int left = 0;
   int right = n - 1;

   while (left <= right) {
	int mid = (left + right) / 2;

	if (array[mid] == targetVal) {
	    return mid;
	}

	if (array[mid] < targetVal) {
	    left = mid + 1;
	} else {
	    right = mid - 1;
	}
   }

   return -1;
}

static inline int interpol_search(int *array, size_t n, int targetVal) {

   int low = 0;
   int high = n - 1;

   // The target must stay within the bounds of the array/vector
   // This condition also avoids out-of-bounds errors and infinite loops
   while (low <= high && targetVal >= array[low] && targetVal <= array[high]) {
	if (array[low] == array[high]) {
	    if (array[low] == targetVal) return low;
	    break;
	}

	int pos = low + (((double)(high - low) / (array[high] - array[low])) * (targetVal - array[low]));

	if (array[pos] == targetVal) {
	    return pos;
        }

	if (array[pos] < targetVal) {
	    low = pos + 1;
	} else {
	    high = pos - 1;
	}
   }

   return -1;
}

static inline size_t expo_search(const int *array, size_t n, int targetVal) {

   if (n == 0)
       return SIZE_MAX;

   size_t bound = 1;

   while (bound < n && array[bound] < targetVal) {
	if (bound > n / 2) {
	    bound = n; // Avoids overflow while doubling
	    break;
	}
	bound *= 2;
   }

   size_t low = bound / 2;
   size_t high = bound < n ? bound : n - 1;

   while (low <= high) {
	size_t mid = low + (high - low) / 2;

	if (array[mid] == targetVal)
	    return mid;
	if (array[mid] < targetVal)
	    low = mid + 1;
	else
	    high = mid - 1;
   }

   return SIZE_MAX;
}

static inline size_t simd_search(const int *array, size_t n, int targetVal) {

// IMPORTANT NOTE: COMMENT THIS FUNCTION IF CROSS-COMPILING TO RISC-V AS RISC-V DOES NOT SUPPORT AVX2!

   if (!array)
	return SIZE_MAX;

   size_t i = 0;
   const __m256i needle = _mm256_set1_epi32(targetVal);

   for (; i + 8 <= n; i += 8) {
	__m256i values = _mm256_loadu_si256(
	    (const __m256i *)(const void *)(array + i));
	__m256i equal = _mm256_cmpeq_epi32(values, needle);
	unsigned mask = (unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(equal));

	if (mask != 0)
	    return i + (size_t)__builtin_ctz(mask);
   }

   // Check any elements left over after the SIMD loop
   for (; i < n; i++)
	if (array[i] == targetVal)
	    return i;

   return SIZE_MAX;
}

static inline size_t fibo_search(const int *array, size_t n, int targetVal) {

   if (!array || n == 0 || n > (size_t)PTRDIFF_MAX)
	return SIZE_MAX;

   size_t fib2 = 0; // F(k - 2)
   size_t fib1 = 1; // F(k - 1)
   size_t fib = fib2 + fib1; // F(k)

   while (fib < n) {
       fib2 = fib1;
       fib1 = fib;
       fib = fib2 + fib1;
   }

   ptrdiff_t offset = -1;

   while (fib > 1) {
	size_t probe = (size_t)(offset + (ptrdiff_t)fib2);
	if (probe >= n)
	    probe = n - 1;

	if (array[probe] < targetVal) {
	    fib = fib1;
	    fib1 = fib2;
	    fib2 = fib - fib1;
	    offset = (ptrdiff_t)probe;
	} else if (array[probe] > targetVal) {
	    fib = fib2;
	    fib1 -= fib2;
	    fib2 = fib - fib1;
	} else {
	    return probe;
	}
   }

   size_t next = (size_t)(offset + 1);
   if (fib1 && next < n && array[next] == targetVal)
       return next;

   return SIZE_MAX;
}

#define sort(arr, SORT_SIZE, algorithm) _Generic((arr), \
    int*: sort_##algorithm##_int,			\
    float*: sort_##algorithm##_float			\
)(arr, SORT_SIZE)

#define search(algorithm, arr, n, targetVal) _Generic((arr), \
    int*: algorithm##_search,				     \
    const int*: algorithm##_search				\
)((arr), (n), (targetVal))

#endif
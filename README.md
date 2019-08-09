# x86PagingTest
A Basic 32-bit (NO PAE) page allocator / mapping system.

- NOTE: This is NOT A REAL page allocator. It is a project to improve my understanding of x86 Paging.

## Motive:
I wanted to find out how x86 paging works, so I created a page mapping program that "pretends" to map out pages of memory.
This application just maps out "page-like" memory chunks (4KB) for a pre-allocated chunk of heap memory, so I could better understand how x86 paging works. 

It uses page directories and page tables like a "real" page allocator, so I could grasp the concept and purpose of these data structures in a real operating system.


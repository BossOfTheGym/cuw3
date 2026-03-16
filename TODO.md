*** build ***
* definitions via options
* benchmarking (maybe)

*** testing ***
* test vmem on windows

*** implementation ***
* allocator foundation
* thread-local allocator foundation 

*** check ***
* check alocation consistency

*** ??? ***
* error code return type in vmem
* alignment issue within FastArena: how to automatically align stuff and not to pad things manually
* const and non-const within the view: what to do?
* layout control vs boilerplate: study boost::intrusive and how its done
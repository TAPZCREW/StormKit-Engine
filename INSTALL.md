# Installation

A C++26 compliant compiler with std module support and XMake is needed to build StormKit

## Dependencies
- [StormKit](https://github.com/TapzCrew/StormKit)

## Building
```
> xmake f -m <release|debug|releasedbg> -k <shared|static> --runtimes=<c++_shared|c++_static|libstdc++_shared|libstdc++_static|MT|MD|MTd|MDd>
> xmake b
```

## Parameters
You can customize your build (with --option=value) with the following parameters 

|       Variable        |         Description             |                                   Default value                                       |
|-----------------------|---------------------------------|---------------------------------------------------------------------------------------|

## usage

```bash
git clone --recursive https://github.com/hugofloresgarcia/kiwi-reaper.git
```

## building

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release         \ # Debug, Release, RelWithDebInfo or MinSizeRel
  -DCMAKE_OSX_ARCHITECTURES=x86_64   \ # i386, x86_64 or arm64
  -DCMAKE_OSX_DEPLOYMENT_TARGET=10.5 \ # lowest supported macOS version
  -DCMAKE_INSTALL_PREFIX="/path/to/reaper/resource/directory"
```
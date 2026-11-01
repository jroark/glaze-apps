# Browser HTTP/TLS transport (copied)

Chat uses the Glaze Browser's bounded HTTP/1.1 and TLS transport, without its
renderer. These files are unmodified copies from
[glaze-os](https://github.com/jroark/glaze-os) at commit `e01281a`:

| File | glaze-os source |
| --- | --- |
| `http.c`, `conn.c`, `tls.c`, `url.c`, `net.h`, `plat.h`, `web.h`, `url.h`, `identity.h` | `apps/browser/` |
| `inflate.c` | `apps/appstore/` |
| `stb_image.h` | `third_party/stb/` (public domain / MIT; its zlib inflater is used) |

Keep them byte-identical to glaze-os so fixes can be re-synced:

```sh
for f in http.c conn.c tls.c url.c net.h plat.h web.h url.h identity.h; do
    cp ../glaze-os/apps/browser/$f packages/chatgpt/http/; done
cp ../glaze-os/apps/appstore/inflate.c ../glaze-os/third_party/stb/stb_image.h packages/chatgpt/http/
```

Chat itself provides the platform hooks they need (`plat.h`) in `platform.c`.
The TLS code links the system's `libwolfssl.so.1` from the SDK.

# Half the textures still miss the cache

Build `Sep 17 2026 00:32:20`.

    OK  build=Sep_17_2026_00-32-20 frames=14400 draws=257 modules=7

    TEXSRC  rendertarget=172 guest=172
    TEXUP   n=172 flat=172: [1024x576 FLAT 0x00000000] ...
    PEEK    [#0 1280x720 varied=0] [#1 854x480 varied=0]
            [#2 1024x576 varied=0] [#3 1024x576 varied=0]
    GPUCNT  vertices=992 vsinv=992 clipin=496 clipout=496
            fsinv=200355840 samples=128471040

The fix works, for exactly half of them. 172 textures are now served from the
surface cache -- the image the GPU rendered -- against none before. The other
172 still take the upload path, and every one of those is 1024x576 and empty:
the same size as the cached render targets.

An even split on surfaces of that size means the lookup key differs somewhere.
The key is address, width, height and pitch, and all four must match. Width and
height clearly do.

Nothing on screen changed, and the surfaces are all still uniform.

## The other thing these numbers say

992 vertices and 496 primitives over the run. 992 / 4 = 248 draws, and
248 x 2 = 496 triangles. Every draw a four-vertex strip, every strip two
triangles, with no remainder anywhere.

Which is consistent with every draw in the run being a full-screen quad, and
no scene geometry ever being submitted. DRAWIN already showed the ones it
sampled to be exactly that: pixel-space quads with orthographic matrices and
texture coordinates, which is a composite, not a model.

If that holds for all of them, "the screen is black" stops being a graphics
fault. It becomes a composite chain running correctly over a scene nobody
drew -- and the engine has only just started decompressing archives at all
(`INFLATE lzma n=139`, the first non-zero in the project's history), so a
level that has not loaded has no geometry to submit.

The arithmetic is consistent with that and does not establish it. A run of
large draws and a matching run of tiny ones average out identically.

## What is being measured next

Build `Sep 17 2026 08:45:13`. Two independent readings, neither a fix:

  * `TEXUP` now prints each missing texture's full descriptor -- address,
    pitch, mip count, tile mode -- and `SURFKEYS` prints what the cache holds,
    so the miss can be compared field by field instead of guessed at.
  * `DRAWSIZE quad= small= mid= big= max=` counts draws by vertex count.
    All in `quad` means no scene geometry was submitted at all, and the
    remaining graphics question is much smaller than it looks.

Deliberately not fixing the texture miss yet. Widening the lookup key to make
it match is a one-line change that would also make it match things it should
not, and which field actually differs is one run away.

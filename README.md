# Cobble

Ever needed to scramble an image and then re-encode it? Cobble is the perfect tool for the job. With Cobble, reading a JPEG or WebP, blitting pieces to arbitrary locations, and re-encoding it to a PNG is an easy task.

```typescript
import { Cobbler } from "@hereiskevin/cobble";

const imageData: Uint8Array = ...; // Some encoded image data.
const cobbler = await Cobbler.decode(imageData);

// Repeat as needed.
const from = { left: 0, top: 0, width: 100, height: 200 };
const to = { left: 0, top: 100, width: 100, height: 200 };
cobbler.cobble(from, to);

const result = await cobbler.encode();
```

Additionally, by utilizing Node.js asynchronous workers, Cobble processes images out of your way, allowing your program to do something else while it waits on the big and slow PNG encode.

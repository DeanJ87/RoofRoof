# Contract: Firmware Image Marker

Every firmware image carries one marker, so an upload can be checked before anything is written.
It follows SQMeter spec 027 FR-020/021, with a product field added.

```
<16-byte RoofRoof magic>product=roofroof;layout=l2;build=standard;\0
```

- The magic differs from SQMeter's, so neither product's scanner mistakes the other's marker for
  its own. The library stores the magic XOR 0xA5, so the image of the scanner itself never
  contains it. Only `src/FirmwareMarker.cpp` spells it out.
- Fields are `key=value;`, in any order. Unknown keys are ignored, and the whole field string is
  at most 48 characters.
- Check order: product, then layout, then build. The first mismatch decides the message:

| Upload | Result | Message |
|---|---|---|
| RoofRoof l2 standard | accepted | - |
| No RoofRoof marker (e.g. SQMeter, any other image) | refused, 400 | "Not RoofRoof firmware." |
| A marker with another `product` | refused, 400 | "This is firmware for another product (sqmeter)." |
| Another layout | refused, 400 | "Wrong partition layout: flash this one over USB." |

- A web UI (LittleFS) image is checked by size against the layout, as in SQMeter.

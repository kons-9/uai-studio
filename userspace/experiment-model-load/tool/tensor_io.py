import dataclasses
import json

DTYPES = {1: "u1", 2: "i1", 3: "<f4", 4: "<i2", 5: "<u2", 6: "<f2", 7: "<i4"}


def prepare_image(path, model):
    import numpy as np
    from PIL import Image, ImageOps
    if model.input is None or model.preprocessing not in (1, 2) or model.color not in (1, 2, 3):
        raise ValueError("image input requires a v2 image preprocessing descriptor")
    tensor = model.input
    tensor.validate()
    if tensor.layout == 1:
        batch, height, width, channels = tensor.shape
    elif tensor.layout == 2:
        batch, channels, height, width = tensor.shape
    else:
        raise ValueError("image input requires NHWC or NCHW")
    if batch != 1 or channels != (1 if model.color == 3 else 3):
        raise ValueError("invalid image tensor shape")
    if width * height * channels > 0x200000:
        raise ValueError("image exceeds reserved input capacity")
    with Image.open(path) as source:
        image = ImageOps.exif_transpose(source).convert("L" if channels == 1 else "RGB")
        original_size = image.size
        resized_size = (width, height)
        left = top = 0
        if model.preprocessing == 1:
            image = image.resize((width, height), Image.Resampling.BILINEAR)
        else:
            ratio = min(width / image.width, height / image.height)
            resized_size = (max(1, min(width, round(image.width * ratio))), max(1, min(height, round(image.height * ratio))))
            resized = image.resize(resized_size, Image.Resampling.BILINEAR)
            left, top = (width - resized.width) // 2, (height - resized.height) // 2
            padded = Image.new(image.mode, (width, height), model.padding if channels == 1 else (model.padding,) * 3)
            padded.paste(resized, (left, top))
            image = padded
        pixels = np.asarray(image, dtype=np.float32)
    if channels == 1:
        pixels = pixels[..., None]
    elif model.color == 2:
        pixels = pixels[..., ::-1]
    values = (pixels - np.asarray(model.mean[:channels])) / np.asarray(model.divisor[:channels])
    dtype = np.dtype(DTYPES[tensor.type])
    if np.issubdtype(dtype, np.integer):
        limits = np.iinfo(dtype)
        values = np.clip(np.rint(values / tensor.scale + tensor.zero), limits.min, limits.max)
    if tensor.layout == 2:
        values = values.transpose(2, 0, 1)
    data = values.astype(dtype).tobytes(order="C")
    if len(data) != model.input_bytes:
        raise ValueError("prepared tensor size mismatch")
    return data, {"source_size": list(original_size), "resized_size": list(resized_size), "padding": [left, top]}


def save_result(directory, header, model, data, crc, image_metadata=None):
    import numpy as np
    from PIL import Image
    import zlib
    if len(data) != model.output_bytes or not model.outputs or zlib.crc32(data) != crc:
        raise ValueError("result requires matching v2 output descriptors")
    tensors = {}
    for index, tensor in enumerate(model.outputs):
        tensors[f"tensor_{index}"] = np.frombuffer(data, dtype=DTYPES[tensor.type], count=tensor.bytes // np.dtype(DTYPES[tensor.type]).itemsize,
                                                  offset=tensor.offset).reshape(tensor.shape).copy()
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "header.bin").write_bytes(header)
    (directory / "result.bin").write_bytes(data)
    (directory / "result.json").write_text(json.dumps({"model": dataclasses.asdict(model), "output_crc": f"{crc:08x}",
                                                       "image": image_metadata}, indent=2))
    np.savez(directory / "tensors.npz", **tensors)
    for index, tensor in enumerate(model.outputs):
        if tensor.role != 4 or tensor.layout not in (1, 2) or len(tensor.shape) != 4 or tensor.shape[0] != 1:
            continue
        values = tensors[f"tensor_{index}"][0]
        if values.shape[-1 if tensor.layout == 1 else 0] < 2:
            raise ValueError("role=4 requires multi-class logits; binary masks need an explicit threshold decoder")
        classes = values.argmax(axis=-1 if tensor.layout == 1 else 0)
        if classes.max() > 65535:
            raise ValueError("segmentation class index exceeds PNG mask capacity")
        Image.fromarray(classes.astype(np.uint8 if classes.max() <= 255 else np.uint16)).save(directory / f"classes_{index}.png")
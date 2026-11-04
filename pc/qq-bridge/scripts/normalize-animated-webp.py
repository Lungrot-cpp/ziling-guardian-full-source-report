import sys
from PIL import Image, ImageOps

source, target = sys.argv[1:3]
count, columns, rows, tile_width = map(int, sys.argv[3:])
with Image.open(source) as image:
    if image.format != "WEBP" or not 1 < image.n_frames <= 10000:
        raise ValueError("Expected an animated WebP with at most 10000 frames")
    tile_height = max(1, round(image.height * tile_width / image.width))
    sheet = Image.new("RGB", (columns * (tile_width + 4) + 4, rows * (tile_height + 4) + 4), "white")
    for index in range(count):
        image.seek(round(index * (image.n_frames - 1) / (count - 1)))
        frame = ImageOps.contain(image.convert("RGBA"), (tile_width, tile_height))
        position = (4 + index % columns * (tile_width + 4), 4 + index // columns * (tile_height + 4))
        sheet.paste(frame, position, frame)
    sheet.save(target, "JPEG", quality=85)

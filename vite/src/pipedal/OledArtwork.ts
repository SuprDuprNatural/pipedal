// Copyright (c) 2026. SPDX-License-Identifier: MIT
export const OLED_ARTWORK_WIDTH = 128;
export const OLED_ARTWORK_HEIGHT = 40;
export const OLED_ARTWORK_BYTES = OLED_ARTWORK_WIDTH * OLED_ARTWORK_HEIGHT / 8;

export function validateArtwork(value: unknown): value is number[] {
    return Array.isArray(value) && value.length === OLED_ARTWORK_BYTES &&
        value.every(v => Number.isInteger(v) && v >= 0 && v <= 255);
}

export function convertArtwork(image: ImageBitmap, crop: boolean, threshold: number, invert: boolean): number[] {
    const canvas = document.createElement("canvas");
    canvas.width = OLED_ARTWORK_WIDTH; canvas.height = OLED_ARTWORK_HEIGHT;
    const context = canvas.getContext("2d", { willReadFrequently: true })!;
    context.fillStyle = invert ? "white" : "black";
    context.fillRect(0, 0, OLED_ARTWORK_WIDTH, OLED_ARTWORK_HEIGHT);
    const scale = (crop ? Math.max : Math.min)(
        OLED_ARTWORK_WIDTH / image.width, OLED_ARTWORK_HEIGHT / image.height);
    const width = image.width * scale, height = image.height * scale;
    context.drawImage(image, (OLED_ARTWORK_WIDTH - width) / 2,
        (OLED_ARTWORK_HEIGHT - height) / 2, width, height);
    const rgba = context.getImageData(
        0, 0, OLED_ARTWORK_WIDTH, OLED_ARTWORK_HEIGHT).data;
    const bytes = Array<number>(OLED_ARTWORK_BYTES).fill(0);
    for (let pixel = 0; pixel < OLED_ARTWORK_WIDTH * OLED_ARTWORK_HEIGHT; ++pixel) {
        const i = pixel * 4;
        const bright = 0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2] >= threshold;
        if (bright !== invert) bytes[pixel >> 3] |= 0x80 >> (pixel % 8);
    }
    return bytes;
}

export async function loadArtwork(file: File): Promise<ImageBitmap> {
    if (file.size > 2 * 1024 * 1024) throw new Error("Choose an image no larger than 2 MiB.");
    if (!["image/png", "image/jpeg", "image/webp"].includes(file.type))
        throw new Error("Choose a PNG, JPEG or WebP image.");
    let image: ImageBitmap;
    try { image = await createImageBitmap(file); }
    catch { throw new Error("This image could not be read."); }
    if (image.width < 1 || image.height < 1 || image.width > 2048 || image.height > 2048) {
        image.close();
        throw new Error("The image must be at most 2048 × 2048 pixels.");
    }
    return image;
}

export function presetNameLines(name: string): string[] {
    let text = Array.from(name).slice(0, 43).map(c => /^[\x20-\x7e]$/.test(c) ? c : "?").join("") || "UNTITLED";
    if (text.length > 42) text = text.slice(0, 39) + "...";
    if (text.length <= 21) return [text, ""];
    let split = text.lastIndexOf(" ", 21);
    if (split <= 0) split = 21;
    let second = text.slice(split + (text[split] === " " ? 1 : 0));
    if (second.length > 21) second = second.slice(0, 18) + "...";
    return [text.slice(0, split), second];
}

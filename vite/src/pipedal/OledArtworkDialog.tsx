// Copyright (c) 2026. SPDX-License-Identifier: MIT
import { useEffect, useRef, useState } from 'react';
import { Alert, Box, Button, Dialog, DialogActions, DialogContent, DialogTitle,
    FormControlLabel, MenuItem, Slider, Stack, Switch, TextField, Typography } from '@mui/material';
import { PiPedalModelFactory } from './PiPedalModel';
import { convertArtwork, loadArtwork, OLED_ARTWORK_HEIGHT, OLED_ARTWORK_WIDTH,
    presetNameLines, validateArtwork } from './OledArtwork';
import { oledGlyphs } from './OledFont';

export default function OledArtworkDialog({ onClose }: { onClose: () => void }) {
    const model = PiPedalModelFactory.getInstance();
    const [target] = useState(() => ({ bankId: model.banks.get().selectedBank,
        presetId: model.presets.get().selectedInstanceId, name: model.pedalboard.get().name }));
    const [artwork, setArtwork] = useState<number[] | undefined>(() => {
        const value = model.pedalboard.get().oledArtwork;
        return validateArtwork(value) ? [...value] : undefined;
    });
    const [image, setImage] = useState<ImageBitmap>();
    const [crop, setCrop] = useState(false);
    const [threshold, setThreshold] = useState(128);
    const [invert, setInvert] = useState(false);
    const [busy, setBusy] = useState(false);
    const [loading, setLoading] = useState(false);
    const [error, setError] = useState("");
    const loadSequence = useRef(0);
    const saving = useRef(false);
    const [canvas, setCanvas] = useState<HTMLCanvasElement | null>(null);
    useEffect(() => () => { ++loadSequence.current; }, []);
    useEffect(() => () => image?.close(), [image]);
    useEffect(() => {
        if (image) setArtwork(convertArtwork(image, crop, threshold, invert));
    }, [image, crop, threshold, invert]);
    useEffect(() => {
        if (!canvas) return; // The dialog portal mounts after the first effect.
        const context = canvas.getContext("2d")!;
        context.fillStyle = "black"; context.fillRect(0, 0, 128, 64);
        context.fillStyle = "white";
        // The top 24 pixels are reserved for the live tuner.
        for (let x = 0; x < 128; x += 16) context.fillRect(x, 12, 8, 4);
        for (const y of [5, 12, 19]) context.fillRect(63, y, 2, 2);
        if (artwork) {
            for (let y = 0; y < OLED_ARTWORK_HEIGHT; ++y)
                for (let x = 0; x < OLED_ARTWORK_WIDTH; ++x)
                    if (artwork[y * 16 + (x >> 3)] & (0x80 >> (x % 8)))
                        context.fillRect(x, y + 24, 1, 1);
        } else {
            const lines = presetNameLines(target.name);
            const top = lines[1] ? 35 : 40;
            lines.forEach((line, row) => {
                const left = 64 - line.length * 3;
                Array.from(line.toUpperCase()).forEach((letter, i) => {
                    const glyph = oledGlyphs[letter] ?? oledGlyphs['?'];
                    glyph.forEach((column, x) => {
                        for (let y = 0; y < 7; ++y) if (column & (1 << y))
                            context.fillRect(left + i * 6 + x, top + row * 10 + y, 1, 1);
                    });
                });
            });
        }
    }, [artwork, target, canvas]);

    async function choose(file?: File) {
        if (!file) return;
        const sequence = ++loadSequence.current;
        setLoading(true); setError("");
        try {
            const next = await loadArtwork(file);
            if (sequence !== loadSequence.current) { next.close(); return; }
            setImage(next);
        } catch (e) {
            if (sequence === loadSequence.current) setError(String(e instanceof Error ? e.message : e));
        } finally { if (sequence === loadSequence.current) setLoading(false); }
    }
    async function apply() {
        if (saving.current) return;
        saving.current = true; setBusy(true); setError("");
        try { await model.setOledArtwork(target.bankId, target.presetId, artwork); onClose(); }
        catch (e) { setError(String(e instanceof Error ? e.message : e)); }
        finally { saving.current = false; setBusy(false); }
    }
    return <Dialog open fullWidth maxWidth="sm" onClose={() => { if (!busy) onClose(); }}>
        <DialogTitle>OLED artwork</DialogTitle>
        <DialogContent><Stack spacing={2}>
            <Typography>{target.name}</Typography>
            <Typography variant="body2">Choose a PNG, JPEG or WebP up to 2 MiB and 2048 × 2048 pixels.</Typography>
            <Box><Button component="label" disabled={busy}>Choose image
                <input hidden type="file" accept="image/png,image/jpeg,image/webp" onChange={e => {
                    void choose(e.target.files?.[0]); e.target.value = "";
                }} />
            </Button><Button disabled={busy || (!artwork && !loading)} onClick={() => {
                ++loadSequence.current; setLoading(false); setImage(undefined); setArtwork(undefined); setError("");
            }}>Remove</Button></Box>
            {image && <>
                <TextField select label="Framing" value={crop ? "crop" : "fit"} disabled={busy}
                    onChange={e => setCrop(e.target.value === "crop")}>
                    <MenuItem value="fit">Fit whole image</MenuItem><MenuItem value="crop">Crop to fill</MenuItem>
                </TextField>
                <Typography id="oled-threshold">Threshold: {threshold}</Typography>
                <Slider aria-labelledby="oled-threshold" min={1} max={255} value={threshold} disabled={busy}
                    onChange={(_, value) => setThreshold(value as number)} />
                <FormControlLabel label="Invert" control={<Switch checked={invert} disabled={busy}
                    onChange={e => setInvert(e.target.checked)} />} />
            </>}
            <Typography variant="body2">Tuner layout preview · 128 × 64 pixels, enlarged</Typography>
            <canvas ref={setCanvas} width={128} height={64} role="img" aria-label={`OLED preview for ${target.name}`}
                style={{ width: 384, maxWidth: '100%', imageRendering: 'pixelated', background: '#000', aspectRatio: '2 / 1' }} />
            <Typography variant="body2">The tuner stays visible above the picture. When artwork is present it fills the lower display and replaces the preset name. Apply, then save the preset to keep it.</Typography>
            {!model.gpioSettings.get().display.presetArtwork &&
                <Alert severity="info">Enable Preset artwork in Hardware display settings to show pictures on the OLED.</Alert>}
            {loading && <Typography>Reading image…</Typography>}
            {error && <Alert severity="error">{error}</Alert>}
        </Stack></DialogContent>
        <DialogActions><Button disabled={busy} onClick={onClose}>Cancel</Button>
            <Button disabled={busy || loading} onClick={() => void apply()}>Apply</Button></DialogActions>
    </Dialog>;
}

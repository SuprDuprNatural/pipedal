// Copyright (c) 2026 SuprPedals contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import React from 'react';
import AirplayIcon from '@mui/icons-material/Airplay';
import IconButton from '@mui/material/IconButton';
import Popover from '@mui/material/Popover';
import Switch from '@mui/material/Switch';
import Slider from '@mui/material/Slider';
import Typography from '@mui/material/Typography';
import useMediaQuery from '@mui/material/useMediaQuery';
import ToolTipEx from './ToolTipEx';
import { PiPedalModel, PiPedalModelFactory } from './PiPedalModel';
import AirplaySettings from './AirplaySettings';

// AirPlay receiver control shown in the main toolbar. The stream volume lives
// below the icon so it does not compete with the toolbar instruments.
function AirplayControl() {
    const model: PiPedalModel = PiPedalModelFactory.getInstance();

    const [settings, setSettings] = React.useState<AirplaySettings>(model.airplaySettings.get());
    const [canAirplay, setCanAirplay] = React.useState<boolean>(model.canAirplay.get());
    const [dragVolume, setDragVolume] = React.useState<number | null>(null);
    const [volumeAnchor, setVolumeAnchor] = React.useState<HTMLElement | null>(null);

    React.useEffect(() => {
        const settingsHandler = (value: AirplaySettings) => setSettings(value);
        const canAirplayHandler = (value: boolean) => setCanAirplay(value);
        model.airplaySettings.addOnChangedHandler(settingsHandler);
        model.canAirplay.addOnChangedHandler(canAirplayHandler);
        return () => {
            model.airplaySettings.removeOnChangedHandler(settingsHandler);
            model.canAirplay.removeOnChangedHandler(canAirplayHandler);
        };
    }, [model]);

    const wideEnough = useMediaQuery('(min-width:600px)');
    if (!canAirplay || !wideEnough) {
        return null;
    }

    const volume = dragVolume !== null ? dragVolume : settings.volume;

    return (
        <div style={{ display: "flex", flexFlow: "row nowrap", alignItems: "center", flex: "0 0 auto", marginRight: 12 }}>
            <ToolTipEx title="AirPlay volume">
                <IconButton color="inherit" size="small"
                    aria-label="AirPlay volume"
                    aria-haspopup="dialog"
                    aria-expanded={volumeAnchor ? true : undefined}
                    onClick={(event) => setVolumeAnchor(event.currentTarget)}>
                    <AirplayIcon style={{ opacity: 0.75 }} fontSize="small" />
                </IconButton>
            </ToolTipEx>
            <ToolTipEx title="AirPlay receiver">
                <Switch
                    size="small"
                    color="default"
                    checked={settings.enabled}
                    style={{ marginLeft: 4 }}
                    onChange={(event) => {
                        model.setAirplayEnabled(event.target.checked);
                    }}
                    inputProps={{ 'aria-label': 'AirPlay receiver on/off' }}
                />
            </ToolTipEx>
            <Popover open={Boolean(volumeAnchor)} anchorEl={volumeAnchor}
                onClose={() => setVolumeAnchor(null)}
                anchorOrigin={{ vertical: 'bottom', horizontal: 'center' }}
                transformOrigin={{ vertical: 'top', horizontal: 'center' }}>
                <div style={{ width: 188, padding: '12px 18px 8px' }}>
                    <Typography variant="caption" component="div" sx={{ mb: .5, opacity: .8 }}>
                        AirPlay volume · {Math.round(volume * 100)}%
                    </Typography>
                    <Slider aria-label="AirPlay volume" size="small"
                        min={0} max={1} step={0.01} value={volume}
                        disabled={!settings.enabled}
                        onChange={(_event, value) => {
                            const next = value as number;
                            setDragVolume(next);
                            model.previewAirplayVolume(next);
                        }}
                        onChangeCommitted={(_event, value) => {
                            setDragVolume(null);
                            model.setAirplayVolume(value as number);
                        }}
                        sx={{ color: 'text.primary', width: '100%',
                            '& .MuiSlider-thumb': { width: 12, height: 12 } }}
                    />
                </div>
            </Popover>
        </div>
    );
}

export default AirplayControl;

// Copyright (c) 2026 SuprDuprNatural
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
import ButtonBase from '@mui/material/ButtonBase';
import Typography from '@mui/material/Typography';
import SelectHoverBackground from './SelectHoverBackground';
import RadioSelectDialog from './RadioSelectDialog';
import { PiPedalModel, PiPedalModelFactory } from './PiPedalModel';
import AirplaySettings from './AirplaySettings';
import JackConfiguration from './Jack';

const MAIN_OUTPUT_LABEL = "Main outputs";

// Settings-dialog row that selects where the AirPlay stream is mixed:
// PiPedal's main outputs, or a specific pair of device output channels.
function AirplayOutputSelect(props: { className?: string }) {
    const model: PiPedalModel = PiPedalModelFactory.getInstance();

    const [settings, setSettings] = React.useState<AirplaySettings>(model.airplaySettings.get());
    const [canAirplay, setCanAirplay] = React.useState<boolean>(model.canAirplay.get());
    const [jackConfiguration, setJackConfiguration] = React.useState<JackConfiguration>(model.jackConfiguration.get());
    const [dialogOpen, setDialogOpen] = React.useState<boolean>(false);

    React.useEffect(() => {
        const settingsHandler = (value: AirplaySettings) => setSettings(value);
        const canAirplayHandler = (value: boolean) => setCanAirplay(value);
        const configurationHandler = (value: JackConfiguration) => setJackConfiguration(value);
        model.airplaySettings.addOnChangedHandler(settingsHandler);
        model.canAirplay.addOnChangedHandler(canAirplayHandler);
        model.jackConfiguration.addOnChangedHandler(configurationHandler);
        return () => {
            model.airplaySettings.removeOnChangedHandler(settingsHandler);
            model.canAirplay.removeOnChangedHandler(canAirplayHandler);
            model.jackConfiguration.removeOnChangedHandler(configurationHandler);
        };
    }, [model]);

    if (!canAirplay) {
        return null;
    }

    const outputCount = jackConfiguration.isValid ? jackConfiguration.outputAudioPorts.length : 0;
    const items: string[] = [MAIN_OUTPUT_LABEL];
    const channelForItem = (item: string): number => {
        if (item === MAIN_OUTPUT_LABEL) return -1;
        const match = /^Outputs (\d+)\//.exec(item);
        return match ? parseInt(match[1]) - 1 : -1;
    };
    for (let i = 0; i + 1 < outputCount; i += 2) {
        items.push(`Outputs ${i + 1}/${i + 2}`);
    }
    const currentItem =
        settings.outputChannel >= 0 && settings.outputChannel + 1 < outputCount
            ? `Outputs ${settings.outputChannel + 1}/${settings.outputChannel + 2}`
            : MAIN_OUTPUT_LABEL;

    return (
        <div>
            <ButtonBase className={props.className} onClick={() => setDialogOpen(true)} >
                <SelectHoverBackground selected={false} showHover={true} />
                <div style={{ width: "100%" }}>
                    <Typography display="block" variant="body2" noWrap>AirPlay output</Typography>
                    <Typography display="block" variant="caption" color="textSecondary" noWrap>{currentItem}</Typography>
                </div>
            </ButtonBase>
            {dialogOpen && (
                <RadioSelectDialog
                    open={dialogOpen}
                    title="AirPlay Output"
                    width={300}
                    selectedItem={currentItem}
                    items={items}
                    onOk={(selectedItem) => {
                        setDialogOpen(false);
                        model.setAirplayOutputChannel(channelForItem(selectedItem));
                    }}
                    onClose={() => setDialogOpen(false)}
                />
            )}
        </div>
    );
}

export default AirplayOutputSelect;

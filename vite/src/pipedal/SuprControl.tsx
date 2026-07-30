// Central dispatcher for controls on Supr faces. Layout code names a port;
// this component chooses the continuous knob, detented trim, hardware button
// or categorical selector while PiPedal's UiControl remains authoritative for
// ranges, log tapers, labels and scale points.

import { ReactNode } from 'react';
import { ControlType } from './Lv2Plugin';
import { PiPedalModelFactory } from './PiPedalModel';
import SuprButton from './SuprButton';
import SuprKnob, { SuprKnobMarks } from './SuprKnob';
import SuprSelect from './SuprSelect';
import SuprStepKnob from './SuprStepKnob';

export interface SuprControlProps {
    instanceId: number;
    uri: string;
    symbol: string;
    value?: number;
    fallback?: ReactNode;
    marks?: SuprKnobMarks;
    markCount?: number;
    step?: number;
    showReadout?: boolean;
    showPointer?: boolean;
    compact?: boolean;
    wide?: boolean;
    hideLabel?: boolean;
    buttonText?: string;
}

export default function SuprControl(props: SuprControlProps) {
    const model = PiPedalModelFactory.getInstance();
    const plugin = model.getUiPlugin(props.uri);
    const uiControl = plugin?.controls.find((control) =>
        control.symbol === props.symbol
    );

    if (!uiControl)
        return (<>{props.fallback}</>);

    const metadataStep = uiControl.range_steps > 1
        ? (uiControl.max_value - uiControl.min_value) / (uiControl.range_steps - 1)
        : undefined;
    const smallIntegerStep = uiControl.controlType === ControlType.Dial
        && uiControl.integer_property
        && Math.abs(uiControl.max_value - uiControl.min_value) <= 16
        ? 1 : undefined;
    const step = props.step ?? metadataStep ?? smallIntegerStep;

    if (step !== undefined
        && (uiControl.controlType === ControlType.Dial
            || uiControl.controlType === ControlType.Select)) {
        return (
            <SuprStepKnob
                instanceId={props.instanceId}
                symbol={uiControl.symbol}
                value={props.value ?? uiControl.default_value}
                label={uiControl.name}
                step={step}
                min={uiControl.min_value}
                max={uiControl.max_value}
                unit={uiControl.getDisplayUnits()}
                showReadout={props.showReadout}
                variant={props.showPointer ? "full" : "minimal"}
                defaultValue={uiControl.default_value}
                formatValue={(value) => uiControl.formatDisplayValue(value)} />
        );
    }

    switch (uiControl.controlType) {
        case ControlType.Dial:
            return (
                <SuprKnob
                    instanceId={props.instanceId}
                    uiControl={uiControl}
                    value={props.value ?? uiControl.default_value}
                    marks={props.marks}
                    markCount={props.markCount} />
            );
        case ControlType.OnOffSwitch:
        case ControlType.ABSwitch:
            return (
                <SuprButton
                    instanceId={props.instanceId}
                    uiControl={uiControl}
                    value={props.value ?? uiControl.default_value}
                    compact={props.compact}
                    hideLabel={props.hideLabel}
                    buttonText={props.buttonText} />
            );
        case ControlType.Select:
            return (
                <SuprSelect
                    instanceId={props.instanceId}
                    uiControl={uiControl}
                    value={props.value ?? uiControl.default_value}
                    compact={props.compact}
                    wide={props.wide} />
            );
        default:
            return (<>{props.fallback}</>);
    }
}

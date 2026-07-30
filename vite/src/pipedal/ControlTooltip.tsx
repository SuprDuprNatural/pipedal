
import React, { ReactElement } from 'react';
import { ControlType, UiControl } from './Lv2Plugin';
import Typography from "@mui/material/Typography";
import Divider from '@mui/material/Divider';
import ToolTipEx from './ToolTipEx'


interface ControlTooltipProps {
    children: ReactElement,
    uiControl: UiControl
    valueTooltip?: React.ReactNode;
}


export default function ControlTooltip(props: ControlTooltipProps) {
    let { children, uiControl, valueTooltip } = props;
    // Dropdowns get no tooltip. Opening the menu takes the pointer away
    // through a portal, so the hover state the tooltip is tracking never
    // gets a clean end and the tip can be left hanging over the page after
    // a selection. A select already shows its own value, so there is
    // nothing here worth that.
    if (uiControl.controlType === ControlType.Select) {
        return children;
    }
    if (uiControl.comment && (uiControl.comment !== uiControl.name)) {
        return (
            <ToolTipEx
                valueTooltip={valueTooltip}
                title={
                    (
                        <React.Fragment>
                            <Typography variant="caption">{uiControl.name}</Typography>
                            <Divider />
                            <Typography variant="caption">{uiControl.comment}</Typography>

                        </React.Fragment>
                    )}
            >
                <div>
                    {children}
                </div>
            </ToolTipEx>
        );
    } else {
        return (
            <ToolTipEx valueTooltip={valueTooltip}
                title={
                    (
                        <Typography variant="caption">{uiControl.name}</Typography>
                    )}
            >
                <div >
                    {children}
                </div>
            </ToolTipEx>
        );
    }
}

// Learn results are recommendations. Applying writes ordinary saved controls.
import { useEffect, useState } from 'react';
import { MonitorPortHandle, PiPedalModelFactory, State } from './PiPedalModel';
import { isDarkMode } from './DarkMode';
export default function SuprLearnActions({instanceId,kind}:{instanceId:number;kind:"clack"|"fuzz"}) {
    const model=PiPedalModelFactory.getInstance();
    const [phase,setPhase]=useState(0);
    const [value,setValue]=useState<number|undefined>(undefined);
    const [sieve,setSieve]=useState(0);
    useEffect(()=>{
        let handles:MonitorPortHandle[]=[];
        let resultHandle:MonitorPortHandle|undefined;
        let generation=0;
        const clear=()=>{++generation;handles.forEach(h=>model.unmonitorPort(h));handles=[];
            if(resultHandle)model.unmonitorPort(resultHandle);resultHandle=undefined;
            setValue(undefined);setPhase(0);};
        const subscribe=()=>{
            clear();const mine=generation;let previousPhase:number|undefined;
            handles.push(model.monitorPort(instanceId,"learn_state",.1,p=>{
                if(mine!==generation || previousPhase===p)return;
                previousPhase=p;setPhase(p);setValue(undefined);
                if(resultHandle)model.unmonitorPort(resultHandle);
                resultHandle=undefined;
                if(p===2)resultHandle=model.monitorPort(instanceId,kind==="clack"?"learned":"learned_gain",.1,v=>{
                    if(mine===generation && Number.isFinite(v))setValue(v);
                });
            }));
            if(kind==="clack")handles.push(model.monitorPort(instanceId,"sieve_state",.1,v=>{if(mine===generation)setSieve(v);}));
        };
        const state=(s:State)=>{if(s===State.Ready)subscribe();else clear();};
        model.state.addOnChangedHandler(state);subscribe();
        return ()=>{clear();model.state.removeOnChangedHandler(state);};
    },[instanceId,kind,model]);
    const dark=isDarkMode();
    const style={minHeight:32,border:"1px solid #777",borderRadius:4,background:dark?"#353535":"#ddd",color:dark?"#eee":"#222",fontFamily:"inherit",fontSize:11};
    return <div style={{maxWidth:300,padding:6,fontSize:11}}>
        {kind==="clack" && <div aria-live="polite">Sieve: {["off","waiting for pitch","protecting attack","pitch stale","settling taps","residual veto","active","arming"][sieve] ?? "waiting"}</div>}
        <div>{kind==="clack"?"Mute strings for 2 seconds.":"Play a representative phrase for 2 seconds."}</div>
        <div style={{display:"flex",gap:6,marginTop:4}}>
            <button type="button" style={style} disabled={phase===1} onClick={()=>{
                setValue(undefined);setPhase(1);
                model.sendPedalboardControlTrigger(instanceId,"learn",1);
            }}>{phase===1?"Learning…":"Learn"}</button>
            <button type="button" style={style} disabled={phase!==2 || value===undefined} onClick={()=>{
                if(value===undefined)return;
                model.setPedalboardControl(instanceId,kind==="clack"?"thresh":"held_gain",value);
                if(kind==="fuzz")model.setPedalboardControl(instanceId,"match_mode",1);
            }}>Apply {value===undefined?"":`${value.toFixed(1)} dB`}{kind==="fuzz"?" & hold":""}</button>
        </div>
        {phase===3 && <div role="status">{kind==="clack"?"Rejected: notes, loud input, or no measurable noise.":"Rejected: insufficient active signal or invalid input."}</div>}
    </div>;
}

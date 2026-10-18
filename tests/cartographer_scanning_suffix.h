int main() {
    GCodes code;
    code.platform.probe.result=GCodeResult::notFinished;
    assert(!code.TakeScanningProbeReading(0xfffffff0u,1));
    assert(code.gridAxis0Index==0 && reprap.move.map.writes==0 && code.scanningResult==GCodeResult::ok);
    code.platform.probe.result=GCodeResult::error;
    assert(code.TakeScanningProbeReading(0xfffffff0u,1));
    assert(code.platform.probe.lastTime==0xfffffff0u && code.scanningResult==GCodeResult::error);
    assert(code.gridAxis0Index==1 && reprap.move.map.writes==0); // first error is latched, bad point never stored
    code.platform.probe.result=GCodeResult::ok;
    assert(code.TakeScanningProbeReading(10,1));
    assert(code.scanningResult==GCodeResult::error && reprap.move.map.height==-0.25f && reprap.move.map.x==1);
    code.gridAxis0Index=3;code.gridAxis1Index=1;code.lastAxis0Index=0;
    assert(code.TakeScanningProbeReading(20,1) && code.gridAxis0Index==2);
    reprap.move.pending=false;
    const unsigned writes=reprap.move.map.writes;
    assert(code.TakeScanningProbeReading(30,1) && code.gridAxis0Index==2 && reprap.move.map.writes==writes);
    reprap.move.pending=true; reprap.move.generation=2;
    assert(code.TakeScanningProbeReading(40,1) && code.gridAxis0Index==2 && reprap.move.map.writes==writes);
    std::puts("RRF production scanning callback: first-error retention, no invalid writes, bounded asynchronous bracket retry, timestamps, serpentine indices and cancellation passed");
}

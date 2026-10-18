int main() {
    auto& manager=platform.endstops; auto& gcodes=reprap.gcodes;
    EndstopOrZProbe probe; probe.hit.action=EndstopHitAction::stopAll;probe.hit.isZProbe=true;
    Move move;manager.activeEndstops=&probe;move.CheckEndstops(true);
    assert(move.stopAll==1 && gcodes.probeTriggers==1 && gcodes.axisTriggers==0 && move.wakes==1);
    probe.hit.probeError=true;manager.activeEndstops=&probe;manager.isHomingMove=true;
    move.CheckEndstops(true);
    assert(manager.ProbeMoveFailed() && move.stopAll==2);
    assert(gcodes.probeTriggers==1 && gcodes.axisTriggers==0); // safety stop cannot become a successful probe or home datum
    for(const auto& drive:move.dms)assert(drive.driversCurrentlyUsed==0);
    manager.probeMoveFailed=false;probe.hit.probeError=false;probe.hit.isZProbe=false;manager.activeEndstops=&probe;
    move.CheckEndstops(true);assert(gcodes.axisTriggers==1);
    GCodeBuffer buffer;
    assert(!gcodes.WaitForEndstopOrProbingMoveToFinish(buffer) && manager.finished==0);
    gcodes.ready=true;
    assert(gcodes.WaitForEndstopOrProbingMoveToFinish(buffer) && manager.finished==1 && gcodes.zHomed);
    manager.probeMoveFailed=true;
    assert(!gcodes.WaitForEndstopOrProbingMoveToFinish(buffer));
    assert(buffer.state==GCodeState::checkError && !buffer.error.empty() && !gcodes.zHomed && !reprap.heat.suspended && gcodes.retractions==1);
    std::puts("RRF production endstop/motion/completion: both-driver safety stop, fault retention, no false trigger/home datum and failed-move cleanup passed");
}

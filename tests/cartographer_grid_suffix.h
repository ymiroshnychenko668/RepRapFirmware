int main() {
    GCodes code;std::string text;StringRef reply(text,1024);
    GCodeBuffer plain;
    assert(code.SelectProbeGridFilename(plain,platform.probe,reply)==GCodeResult::ok && std::string(code.ProbeGridOutputFile())=="heightmap.csv");
    platform.probe.external=true;
    assert(code.SelectProbeGridFilename(plain,platform.probe,reply)==GCodeResult::ok && std::string(code.ProbeGridOutputFile())=="cartographer-v3-heightmap.csv");
    for(const char* bad:{"","heightmap.csv","0:/sys/HEIGHTMAP.CSV","0:/sys/",
        "heightmap.csv.","heightmap.csv ","HEIGHT~1.CSV","0:heightmap.csv",
        "0:/sys/heightmap.csv.","0:/sys/HEIGHT~1.CSV",".hidden.csv",".","..","mesh.","mesh name.csv","mesh:other.csv","карта.csv"}) {
        const unsigned saves=code.saves, ended=platform.ended;
        GCodeBuffer request({{'P',bad}});
        assert(code.SelectProbeGridFilename(request,platform.probe,reply)==GCodeResult::error);
        assert(code.saves==saves && platform.ended==ended && !reprap.move.active && code.cartographerHeightMapFile.strlen()==0);
    }
    for(const char* allowed:{"cartographer-v3-heightmap.csv","Cartographer_V3-Mesh.001.csv","0:/sys/plugin/cartographer-mesh.csv"}) {
        GCodeBuffer request({{'P',allowed}});
        assert(code.SelectProbeGridFilename(request,platform.probe,reply)==GCodeResult::ok && std::string(code.ProbeGridOutputFile())==allowed);
    }
    GCodeBuffer custom({{'P',"0:/sys/plugin/cartographer-mesh.csv"}});
    assert(code.SelectProbeGridFilename(custom,platform.probe,reply)==GCodeResult::ok);
    code.FinishGrid(custom,reply);
    assert(code.result==GCodeResult::ok && code.savedName=="0:/sys/plugin/cartographer-mesh.csv" && reprap.move.active && reprap.move.map.extrapolations==1);
    reprap.move.active=false;code.saveFailed=true;
    code.FinishGrid(custom,reply);
    assert(code.result==GCodeResult::error && !reprap.move.active && reprap.move.map.clears==1 && reprap.move.map.extrapolations==1);
    platform.probe.external=false;GCodeBuffer legacy({{'P',"ignored.csv"}});
    assert(code.SelectProbeGridFilename(legacy,platform.probe,reply)==GCodeResult::ok && std::string(code.ProbeGridOutputFile())=="heightmap.csv");
    code.FinishGrid(legacy,reply);
    assert(code.result==GCodeResult::error && code.savedName=="heightmap.csv" && reprap.move.active); // legacy failure/activation behavior preserved
    const unsigned saves=code.saves;code.scanningResult=GCodeResult::error;
    code.FinishGrid(legacy,reply);assert(code.saves==saves);
    assert(platform.ended==4); // every success/failure completion releases the scan reservation
    std::puts("RRF production G29 completion: isolated output, legacy routing, invalid defaults rejected, save failure cannot activate P12 mesh, failed scans never saved, reservation released");
}

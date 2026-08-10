$SP = "C:\Users\BOSS\AppData\Local\Temp\claude\C--Projects-MCPE-endstone-js\9d39ab17-5b3e-4f12-a1f7-66ce08716155\scratchpad"

$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

# Build
cmd.exe /c "`"$vcvars`" >nul && cmake --build C:\esbuild\node-plugin"

# Remove previous container
docker rm -f endstone-node-test 2>$null

# Start server
docker run -d -i `
    --name endstone-node-test `
    -p 19132:19132/udp `
    -p 19133:19133/udp `
    -v "C:\Projects\MCPE\endstone-js:/src:ro" `
    -v "$SP\libnode:/libnode:ro" `
    -v "$SP\linux-serve.sh:/linux-serve.sh:ro" `
    -v "endstone-bds-linux:/server" `
    endstone-node-spike:bds `
    bash /linux-serve.sh

Write-Host ""
Write-Host "Server started!"
Write-Host "Connect to: localhost:19132"
Write-Host ""
Write-Host "Container: endstone-node-test"
Write-Host "Use 'docker logs -f endstone-node-test' to view logs."

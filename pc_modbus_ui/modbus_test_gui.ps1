Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-ModbusCrc {
    param(
        [byte[]]$Data,
        [int]$Length = $Data.Length
    )

    [uint16]$crc = 0xFFFF
    for ($i = 0; $i -lt $Length; $i++) {
        $crc = $crc -bxor $Data[$i]
        for ($j = 0; $j -lt 8; $j++) {
            if (($crc -band 1) -ne 0) {
                $crc = (($crc -shr 1) -bxor 0xA001)
            } else {
                $crc = ($crc -shr 1)
            }
        }
    }
    return [uint16]$crc
}

function New-ModbusReadRequest {
    param(
        [byte]$SlaveId,
        [byte]$Function,
        [uint16]$Start,
        [uint16]$Count
    )

    $frame = New-Object byte[] 8
    $frame[0] = $SlaveId
    $frame[1] = $Function
    $frame[2] = [byte](($Start -shr 8) -band 0xFF)
    $frame[3] = [byte]($Start -band 0xFF)
    $frame[4] = [byte](($Count -shr 8) -band 0xFF)
    $frame[5] = [byte]($Count -band 0xFF)

    $crc = Get-ModbusCrc -Data $frame -Length 6
    $frame[6] = [byte]($crc -band 0xFF)
    $frame[7] = [byte](($crc -shr 8) -band 0xFF)
    return $frame
}

function Read-Exact {
    param(
        [System.IO.Ports.SerialPort]$SerialPort,
        [int]$Length
    )

    $buffer = New-Object byte[] $Length
    $offset = 0

    while ($offset -lt $Length) {
        $read = $SerialPort.Read($buffer, $offset, $Length - $offset)
        if ($read -le 0) {
            throw "Serial read timeout"
        }
        $offset += $read
    }

    return $buffer
}

function Convert-RegistersToFloat {
    param(
        [uint16]$HighWord,
        [uint16]$LowWord
    )

    [uint32]$raw = ([uint32]$HighWord -shl 16) -bor [uint32]$LowWord
    $bytes = [BitConverter]::GetBytes($raw)
    return [BitConverter]::ToSingle($bytes, 0)
}

function Convert-ToInt16 {
    param([uint16]$Value)

    if ($Value -ge 0x8000) {
        return ($Value - 0x10000)
    }
    return [int]$Value
}

function Parse-ModbusReadResponse {
    param(
        [byte[]]$Response,
        [int]$ExpectedSlaveId,
        [int]$ExpectedFunction
    )

    if ($Response.Length -lt 5) {
        throw "Response too short"
    }

    if ($Response[0] -ne $ExpectedSlaveId) {
        throw "Unexpected slave id: $($Response[0])"
    }

    $crcExpected = Get-ModbusCrc -Data $Response -Length ($Response.Length - 2)
    $crcReceived = [uint16]($Response[-2] -bor ($Response[-1] -shl 8))
    if ($crcExpected -ne $crcReceived) {
        throw ("CRC mismatch. expected=0x{0:X4}, received=0x{1:X4}" -f $crcExpected, $crcReceived)
    }

    if (($Response[1] -band 0x80) -ne 0) {
        throw ("Modbus exception response. code=0x{0:X2}" -f $Response[2])
    }

    if ($Response[1] -ne $ExpectedFunction) {
        throw "Unexpected function code: $($Response[1])"
    }

    $byteCount = $Response[2]
    if ($byteCount -ne ($Response.Length - 5)) {
        throw "Unexpected byte count"
    }

    $registerCount = [int]($byteCount / 2)
    $registers = New-Object 'System.UInt16[]' $registerCount

    for ($i = 0; $i -lt $registerCount; $i++) {
        $hi = $Response[3 + (2 * $i)]
        $lo = $Response[4 + (2 * $i)]
        $registers[$i] = [uint16](($hi -shl 8) -bor $lo)
    }

    return $registers
}

function Invoke-ModbusRead {
    param(
        [System.IO.Ports.SerialPort]$SerialPort,
        [byte]$SlaveId,
        [byte]$Function,
        [uint16]$Start,
        [uint16]$Count
    )

    $request = New-ModbusReadRequest -SlaveId $SlaveId -Function $Function -Start $Start -Count $Count
    $SerialPort.DiscardInBuffer()
    $SerialPort.DiscardOutBuffer()
    $SerialPort.Write($request, 0, $request.Length)

    $header = Read-Exact -SerialPort $SerialPort -Length 3
    $byteCount = $header[2]
    $tail = Read-Exact -SerialPort $SerialPort -Length ($byteCount + 2)

    $response = New-Object byte[] ($header.Length + $tail.Length)
    [Array]::Copy($header, 0, $response, 0, $header.Length)
    [Array]::Copy($tail, 0, $response, $header.Length, $tail.Length)

    $registers = Parse-ModbusReadResponse -Response $response -ExpectedSlaveId $SlaveId -ExpectedFunction $Function

    return @{
        Request = $request
        Response = $response
        Registers = $registers
    }
}

function Format-ByteArrayHex {
    param([byte[]]$Data)

    return (($Data | ForEach-Object { "{0:X2}" -f $_ }) -join ' ')
}

function Decode-MeasurementMap {
    param(
        [uint16[]]$Registers,
        [int]$Start
    )

    $lines = New-Object System.Collections.Generic.List[string]

    for ($i = 0; $i -lt $Registers.Length; $i++) {
        $lines.Add(("0x{0:X4} : 0x{1:X4}" -f ($Start + $i), $Registers[$i]))
    }

    if (($Start -eq 0) -and ($Registers.Length -ge 8)) {
        $coPpm = Convert-RegistersToFloat -HighWord $Registers[0] -LowWord $Registers[1]
        $voutRaw = Convert-ToInt16 -Value $Registers[2]
        $vrefRaw = Convert-ToInt16 -Value $Registers[3]
        $tempC = Convert-RegistersToFloat -HighWord $Registers[4] -LowWord $Registers[5]
        $vsmV = Convert-RegistersToFloat -HighWord $Registers[6] -LowWord $Registers[7]

        $lines.Add("")
        $lines.Add("Decoded values")
        $lines.Add(("CO_PPM   : {0:N3} ppm" -f $coPpm))
        $lines.Add(("VOUT_RAW : {0}" -f $voutRaw))
        $lines.Add(("VREF_RAW : {0}" -f $vrefRaw))
        $lines.Add(("TEMP_C   : {0:N3} C" -f $tempC))
        $lines.Add(("VS_mV    : {0:N3} mV" -f $vsmV))
    }

    return ($lines -join [Environment]::NewLine)
}

function Get-AvailablePorts {
    return [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
}

$form = New-Object System.Windows.Forms.Form
$form.Text = "MSP430 Modbus RTU Test"
$form.StartPosition = "CenterScreen"
$form.Size = New-Object System.Drawing.Size(920, 700)
$form.MinimumSize = New-Object System.Drawing.Size(920, 700)

$font = New-Object System.Drawing.Font("Segoe UI", 9)
$form.Font = $font

$lblPort = New-Object System.Windows.Forms.Label
$lblPort.Text = "COM Port"
$lblPort.Location = New-Object System.Drawing.Point(20, 20)
$lblPort.AutoSize = $true
$form.Controls.Add($lblPort)

$cmbPort = New-Object System.Windows.Forms.ComboBox
$cmbPort.Location = New-Object System.Drawing.Point(20, 42)
$cmbPort.Size = New-Object System.Drawing.Size(120, 24)
$cmbPort.DropDownStyle = "DropDownList"
$form.Controls.Add($cmbPort)

$btnRefresh = New-Object System.Windows.Forms.Button
$btnRefresh.Text = "Refresh"
$btnRefresh.Location = New-Object System.Drawing.Point(150, 41)
$btnRefresh.Size = New-Object System.Drawing.Size(80, 27)
$form.Controls.Add($btnRefresh)

$lblBaud = New-Object System.Windows.Forms.Label
$lblBaud.Text = "Baud"
$lblBaud.Location = New-Object System.Drawing.Point(250, 20)
$lblBaud.AutoSize = $true
$form.Controls.Add($lblBaud)

$txtBaud = New-Object System.Windows.Forms.TextBox
$txtBaud.Location = New-Object System.Drawing.Point(250, 42)
$txtBaud.Size = New-Object System.Drawing.Size(90, 24)
$txtBaud.Text = "9600"
$form.Controls.Add($txtBaud)

$lblTimeout = New-Object System.Windows.Forms.Label
$lblTimeout.Text = "Timeout ms"
$lblTimeout.Location = New-Object System.Drawing.Point(360, 20)
$lblTimeout.AutoSize = $true
$form.Controls.Add($lblTimeout)

$txtTimeout = New-Object System.Windows.Forms.TextBox
$txtTimeout.Location = New-Object System.Drawing.Point(360, 42)
$txtTimeout.Size = New-Object System.Drawing.Size(90, 24)
$txtTimeout.Text = "1000"
$form.Controls.Add($txtTimeout)

$lblSlave = New-Object System.Windows.Forms.Label
$lblSlave.Text = "Slave ID"
$lblSlave.Location = New-Object System.Drawing.Point(20, 88)
$lblSlave.AutoSize = $true
$form.Controls.Add($lblSlave)

$txtSlave = New-Object System.Windows.Forms.TextBox
$txtSlave.Location = New-Object System.Drawing.Point(20, 110)
$txtSlave.Size = New-Object System.Drawing.Size(80, 24)
$txtSlave.Text = "1"
$form.Controls.Add($txtSlave)

$lblFunction = New-Object System.Windows.Forms.Label
$lblFunction.Text = "Function"
$lblFunction.Location = New-Object System.Drawing.Point(120, 88)
$lblFunction.AutoSize = $true
$form.Controls.Add($lblFunction)

$cmbFunction = New-Object System.Windows.Forms.ComboBox
$cmbFunction.Location = New-Object System.Drawing.Point(120, 110)
$cmbFunction.Size = New-Object System.Drawing.Size(100, 24)
$cmbFunction.DropDownStyle = "DropDownList"
[void]$cmbFunction.Items.Add("FC03")
[void]$cmbFunction.Items.Add("FC04")
$cmbFunction.SelectedIndex = 1
$form.Controls.Add($cmbFunction)

$lblStart = New-Object System.Windows.Forms.Label
$lblStart.Text = "Start Addr"
$lblStart.Location = New-Object System.Drawing.Point(240, 88)
$lblStart.AutoSize = $true
$form.Controls.Add($lblStart)

$txtStart = New-Object System.Windows.Forms.TextBox
$txtStart.Location = New-Object System.Drawing.Point(240, 110)
$txtStart.Size = New-Object System.Drawing.Size(90, 24)
$txtStart.Text = "0"
$form.Controls.Add($txtStart)

$lblCount = New-Object System.Windows.Forms.Label
$lblCount.Text = "Count"
$lblCount.Location = New-Object System.Drawing.Point(350, 88)
$lblCount.AutoSize = $true
$form.Controls.Add($lblCount)

$txtCount = New-Object System.Windows.Forms.TextBox
$txtCount.Location = New-Object System.Drawing.Point(350, 110)
$txtCount.Size = New-Object System.Drawing.Size(90, 24)
$txtCount.Text = "8"
$form.Controls.Add($txtCount)

$lblInterval = New-Object System.Windows.Forms.Label
$lblInterval.Text = "Watch ms"
$lblInterval.Location = New-Object System.Drawing.Point(460, 88)
$lblInterval.AutoSize = $true
$form.Controls.Add($lblInterval)

$txtInterval = New-Object System.Windows.Forms.TextBox
$txtInterval.Location = New-Object System.Drawing.Point(460, 110)
$txtInterval.Size = New-Object System.Drawing.Size(90, 24)
$txtInterval.Text = "1000"
$form.Controls.Add($txtInterval)

$btnReadOnce = New-Object System.Windows.Forms.Button
$btnReadOnce.Text = "Read Once"
$btnReadOnce.Location = New-Object System.Drawing.Point(580, 106)
$btnReadOnce.Size = New-Object System.Drawing.Size(100, 30)
$form.Controls.Add($btnReadOnce)

$btnWatch = New-Object System.Windows.Forms.Button
$btnWatch.Text = "Start Watch"
$btnWatch.Location = New-Object System.Drawing.Point(690, 106)
$btnWatch.Size = New-Object System.Drawing.Size(100, 30)
$form.Controls.Add($btnWatch)

$btnClear = New-Object System.Windows.Forms.Button
$btnClear.Text = "Clear Log"
$btnClear.Location = New-Object System.Drawing.Point(800, 106)
$btnClear.Size = New-Object System.Drawing.Size(90, 30)
$form.Controls.Add($btnClear)

$lblDecoded = New-Object System.Windows.Forms.Label
$lblDecoded.Text = "Decoded Output"
$lblDecoded.Location = New-Object System.Drawing.Point(20, 160)
$lblDecoded.AutoSize = $true
$form.Controls.Add($lblDecoded)

$txtDecoded = New-Object System.Windows.Forms.TextBox
$txtDecoded.Location = New-Object System.Drawing.Point(20, 182)
$txtDecoded.Size = New-Object System.Drawing.Size(400, 450)
$txtDecoded.Multiline = $true
$txtDecoded.ScrollBars = "Vertical"
$txtDecoded.ReadOnly = $true
$form.Controls.Add($txtDecoded)

$lblLog = New-Object System.Windows.Forms.Label
$lblLog.Text = "Request / Response Log"
$lblLog.Location = New-Object System.Drawing.Point(440, 160)
$lblLog.AutoSize = $true
$form.Controls.Add($lblLog)

$txtLog = New-Object System.Windows.Forms.TextBox
$txtLog.Location = New-Object System.Drawing.Point(440, 182)
$txtLog.Size = New-Object System.Drawing.Size(450, 450)
$txtLog.Multiline = $true
$txtLog.ScrollBars = "Vertical"
$txtLog.ReadOnly = $true
$form.Controls.Add($txtLog)

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 1000
$script:SerialPort = $null

function Refresh-PortList {
    $ports = Get-AvailablePorts
    $current = $cmbPort.Text
    $cmbPort.Items.Clear()
    foreach ($port in $ports) {
        [void]$cmbPort.Items.Add($port)
    }
    if ($ports.Count -gt 0) {
        if ($ports -contains $current) {
            $cmbPort.SelectedItem = $current
        } else {
            $cmbPort.SelectedIndex = 0
        }
    }
}

function Append-Log {
    param([string]$Text)

    if ($txtLog.TextLength -gt 0) {
        $txtLog.AppendText([Environment]::NewLine + [Environment]::NewLine)
    }
    $txtLog.AppendText($Text)
}

function Close-UiPort {
    if ($script:SerialPort -ne $null) {
        try {
            if ($script:SerialPort.IsOpen) {
                $script:SerialPort.Close()
            }
        } catch {
        }
        $script:SerialPort.Dispose()
        $script:SerialPort = $null
    }
}

function Open-UiPort {
    param([hashtable]$Config)

    if (($script:SerialPort -ne $null) -and $script:SerialPort.IsOpen) {
        if (($script:SerialPort.PortName -eq $Config.Port) -and
            ($script:SerialPort.BaudRate -eq $Config.BaudRate) -and
            ($script:SerialPort.ReadTimeout -eq $Config.TimeoutMs) -and
            ($script:SerialPort.WriteTimeout -eq $Config.TimeoutMs)) {
            return $script:SerialPort
        }
    }

    Close-UiPort

    $script:SerialPort = New-Object System.IO.Ports.SerialPort $Config.Port, $Config.BaudRate, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    $script:SerialPort.ReadTimeout = $Config.TimeoutMs
    $script:SerialPort.WriteTimeout = $Config.TimeoutMs
    $script:SerialPort.DtrEnable = $false
    $script:SerialPort.RtsEnable = $false
    $script:SerialPort.Open()

    Start-Sleep -Milliseconds 150
    $script:SerialPort.DiscardInBuffer()
    $script:SerialPort.DiscardOutBuffer()

    return $script:SerialPort
}

function Read-UiValues {
    $functionCode = if ($cmbFunction.SelectedItem -eq "FC03") { 3 } else { 4 }

    return @{
        Port = [string]$cmbPort.Text
        BaudRate = [int]$txtBaud.Text
        TimeoutMs = [int]$txtTimeout.Text
        SlaveId = [int]$txtSlave.Text
        Function = [int]$functionCode
        Start = [int]$txtStart.Text
        Count = [int]$txtCount.Text
        IntervalMs = [int]$txtInterval.Text
    }
}

function Invoke-UiRead {
    $cfg = Read-UiValues

    if ([string]::IsNullOrWhiteSpace($cfg.Port)) {
        throw "Select a COM port"
    }

    $serialPort = Open-UiPort -Config $cfg

    $result = Invoke-ModbusRead -SerialPort $serialPort -SlaveId ([byte]$cfg.SlaveId) `
        -Function ([byte]$cfg.Function) -Start ([uint16]$cfg.Start) -Count ([uint16]$cfg.Count)

    $txtDecoded.Text = Decode-MeasurementMap -Registers $result.Registers -Start $cfg.Start

    $logText = @(
        "Timestamp : $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff')"
        "Port      : $($cfg.Port)"
        "Slave ID  : $($cfg.SlaveId)"
        "Function  : FC$($cfg.Function)"
        ("Range     : 0x{0:X4} .. 0x{1:X4}" -f $cfg.Start, ($cfg.Start + $cfg.Count - 1))
        "Request   : $(Format-ByteArrayHex -Data $result.Request)"
        "Response  : $(Format-ByteArrayHex -Data $result.Response)"
    ) -join [Environment]::NewLine

    Append-Log -Text $logText
}

$btnRefresh.Add_Click({
    try {
        Close-UiPort
        Refresh-PortList
    } catch {
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Refresh Error")
    }
})

$btnReadOnce.Add_Click({
    try {
        Invoke-UiRead
    } catch {
        Append-Log -Text ("Error : " + $_.Exception.Message)
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Read Error")
    }
})

$btnWatch.Add_Click({
    try {
        if ($timer.Enabled) {
            $timer.Stop()
            $btnWatch.Text = "Start Watch"
        } else {
            $cfg = Read-UiValues
            $timer.Interval = [Math]::Max(200, $cfg.IntervalMs)
            $timer.Start()
            $btnWatch.Text = "Stop Watch"
            Invoke-UiRead
        }
    } catch {
        Append-Log -Text ("Error : " + $_.Exception.Message)
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Watch Error")
    }
})

$btnClear.Add_Click({
    $txtLog.Clear()
    $txtDecoded.Clear()
})

$timer.Add_Tick({
    try {
        Invoke-UiRead
    } catch {
        $timer.Stop()
        $btnWatch.Text = "Start Watch"
        Append-Log -Text ("Error : " + $_.Exception.Message)
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, "Watch Error")
    }
})

$form.Add_Shown({
    Refresh-PortList
})

$form.Add_FormClosing({
    if ($timer.Enabled) {
        $timer.Stop()
    }
    Close-UiPort
})

[void]$form.ShowDialog()

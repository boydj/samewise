*** Comments ***
The XIAO image on Renode's emulated nRF52840 (milestone 3.5): it boots, decodes
the embedded SAME clip to the expected header and EOM, and brackets the decoding
with markers at which the emulation pauses to read the executed-instruction
counter. tools/renode/bench.py turns the counts, the console and the stack report
into docs/xiao-bench.md.

Run through tests/renode/run.sh, which sets ELF, PLATFORM and OUT.

*** Settings ***
Library             OperatingSystem
Resource            ${RENODEKEYWORDS}
Suite Setup         Setup
Suite Teardown      Teardown
Test Teardown       Test Teardown

*** Variables ***
${HEADER}           WX HEADER ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-

*** Keywords ***
Load XIAO
    Execute Command             $elf=@${ELF}
    Execute Command             $platform=@${PLATFORM}
    Execute Command             $uartlog=@${OUT}/uart.log
    Execute Script              ${CURDIR}/xiao.resc
    Create Terminal Tester      sysbus.uart0    timeout=120

Instructions
    ${n}=    Execute Command    sysbus.cpu ExecutedInstructions
    ${n}=    Evaluate           int("""${n}""".strip(), 0)
    RETURN    ${n}

*** Test Cases ***
Boots And Decodes The Embedded Clip
    Load XIAO
    Start Emulation
    Wait For Line On Uart       Pocket WX Radio firmware
    Wait For Line On Uart       WX BENCH START    pauseEmulation=true
    ${start}=    Instructions
    Start Emulation
    Wait For Line On Uart       ${HEADER}
    Wait For Line On Uart       WX EOM
    Wait For Line On Uart       WX BENCH END    pauseEmulation=true
    ${end}=    Instructions
    Create File    ${OUT}/instructions.txt    start=${start}\nend=${end}\n
    Start Emulation
    Wait For Line On Uart       WX BENCH DONE

import os
import hashlib
import json
import runpy
import sys
import xml.etree.ElementTree as ET
from lxml import etree
from pathlib import Path

ROOT=Path(__file__).resolve().parent
def xml_content(node):
    return (node.tag,sorted((k,v) for k,v in node.attrib.items() if not (node.tag.endswith('}project') and k=='sw_version')),(node.text or '').strip(),[xml_content(child) for child in node])

def input_hashes():
    result={}
    for p in ROOT.rglob('*'):
        if (not p.is_file() or any(x in p.parts for x in ('outflow','work_pnr','work_syn','ooc','xsim.dir'))
            or p.suffix.lower() not in ('.v','.sv','.vh','.vhd','.vhdl','.xml','.sdc','.hex','.bin','.f','.py','.cmd')):continue
        data=json.dumps(xml_content(ET.parse(p).getroot()),sort_keys=True).encode() if p.suffix=='.xml' else p.read_bytes()
        result[str(p.relative_to(ROOT))]=hashlib.sha256(data).hexdigest()
    return result

def main():
    efinity=Path(os.environ.get('EFINITY_HOME','D:/Efinity'))
    periphery=ET.parse(ROOT/'v6_person.peri.xml')
    assert periphery.getroot().get('name')=='v6_person'
    assert len([e for e in periphery.iter() if e.tag.endswith('}pll')])==4, 'Missing V6 PLL configuration'
    schema=etree.XMLSchema(etree.parse(str(efinity/'pt/bin/schema/peri_design_db.xsd')))
    schema.assertValid(etree.parse(str(ROOT/'v6_person.peri.xml')))
    os.environ.setdefault('PROCESSOR_ARCHITECTURE','AMD64')
    receipt=ROOT/'outflow/build_receipt.json'
    receipt.unlink(missing_ok=True)
    inputs=input_hashes()
    dll_directory=os.add_dll_directory(str(efinity/'bin'))
    sys.path.insert(0,str(efinity/'scripts'))
    sys.argv=[str(efinity/'scripts/efx_run.py'),'v6_person','--prj','-f','compile']
    try:
        runpy.run_path(sys.argv[0],run_name='__main__')
    except SystemExit as error:
        if error.code not in (None,0):raise
    assert input_hashes()==inputs,'FPGA input content changed during compilation'
    bit=ROOT/'outflow/v6_person.bit'
    receipt.write_text(json.dumps({'inputs':inputs,'bit_sha256':hashlib.sha256(bit.read_bytes()).hexdigest()},indent=2))
    print('FPGA_BUILD_RECEIPT_PASS: input content and output bit recorded')

if __name__=='__main__':main()

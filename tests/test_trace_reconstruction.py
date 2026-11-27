import sys, unittest, tempfile, zipfile, io, csv
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'devtools'))
from trace_reconstruction import analyze, inspect, REQUIRED, summarize_presentation

def row(role, p, im='a', seq='7', w='1384', h='780', **changes):
    r=dict.fromkeys(REQUIRED,'0'); r.update(role=role,present='40',submission='3',**{'pass':str(p)},draws='30',dispatches='10',slot='0',element='0',stage='1',shader='ac',image=im,view='aa',pool='1',write_sequence=seq,aspect='1',mip='0',mips='1',layer='0',layers='1',width=w,height=h,samples='1',status='metadata',guest_offset='0' if im=='a' else '10000',guest_bytes='100',store='0');r.update(changes);return r

def chain(): return [row('color',0),row('sampled-binding',1),row('render-area',1,im='0',w='1728',h='972'),row('color',1,im='b',seq='8',w='1728',h='972')]

class TraceTests(unittest.TestCase):
    def test_candidate_is_never_certified(self):
        c=analyze(chain())['candidates'];self.assertEqual(len(c),1);self.assertFalse(c[0]['certified']);self.assertEqual(c[0]['source_extent'],(1384,780));self.assertEqual(c[0]['destination_extent'],(1728,972))
    def test_draw_state_and_shader_evidence(self):
        r=chain(); r.insert(2,row('draw-state',1)); index={'ac':{'stage':1,'key':'ac','bindings':[]}}
        c=analyze(r,shader_index=index)['candidates'][0]
        self.assertEqual(len(c['draw_state']),1);self.assertEqual(c['static_shader_evidence'],index['ac']);self.assertFalse(c['certified'])
    def test_downstream(self):
        r=chain();r.append(row('sampled-binding',2,im='b',seq='8',w='1728',h='972'))
        self.assertEqual(len(analyze(r)['candidates'][0]['downstream']),1)
        r.insert(-1,row('copy-destination',2,im='b',seq='9',w='1728',h='972'))
        self.assertEqual(len(analyze(r)['candidates'][0]['downstream']),0)
    def test_docked_chain_reaches_exact_presentation_source(self):
        # A scene enlargement into b, followed by composition into c.
        # A separate unrelated enlargement must not be promoted merely
        # because it uses matching dimensions in this captured frame.
        r=chain()
        r += [row('sampled-binding',2,im='b',seq='8',w='1728',h='972'),
              row('render-area',2,im='0',w='1920',h='1080'),
              row('color',2,im='c',seq='9',w='1920',h='1080',guest_offset='20000'),
              row('sampled-binding',3),
              row('color',3,im='d',seq='10',w='1920',h='1080',guest_offset='30000'),
              row('presentation-source',-202,im='c',seq='9',w='1920',h='1080',guest_offset='20000',status='full-image')]
        result=analyze(r)
        by_pass={c['pass_index']:c for c in result['candidates']}
        self.assertTrue(by_pass[1]['presentation_link']['linked'])
        self.assertTrue(by_pass[2]['presentation_link']['linked'])
        self.assertFalse(by_pass[3]['presentation_link']['linked'])
        self.assertEqual(by_pass[1]['presentation_link']['path_rows'],[4,5,7,10])
        self.assertFalse(by_pass[1]['certified'])
    def test_load_preserves_previous_scene_write(self):
        # Two enlargements followed by a LOAD/STORE overlay pass on the
        # exact same attachment, as observed in the FFR docked capture.
        r=chain()+[row('sampled-binding',2,im='b',seq='8',w='1728',h='972'),
            row('color',2,im='c',seq='9',w='1920',h='1080',guest_offset='20000'),
            row('color',3,im='c',seq='11',w='1920',h='1080',guest_offset='20000',load='0'),
            row('presentation-source',-202,im='c',seq='11',w='1920',h='1080',guest_offset='20000',status='full-image')]
        by_pass={c['pass_index']:c for c in analyze(r)['candidates']}
        self.assertTrue(by_pass[1]['presentation_link']['linked'])
        self.assertTrue(by_pass[2]['presentation_link']['linked'])
        self.assertFalse(by_pass[1]['certified'])
        # CLEAR/DONT_CARE must not preserve old color.
        for load in ('1','2'):
            r[-2]['load']=load
            self.assertFalse(analyze(r)['candidates'][0]['presentation_link']['linked'])
        r[-2]['load']='0'
        r.insert(-2,row('copy-destination',5,im='d',seq='10',guest_offset='20025'))
        self.assertFalse(analyze(r)['candidates'][0]['presentation_link']['linked'])

    def test_presentation_chain_rejects_stale_and_alias_overwrites(self):
        r=chain()+[row('presentation-source',-202,im='b',seq='9',w='1728',h='972',status='full-image')]
        self.assertFalse(analyze(r)['candidates'][0]['presentation_link']['linked'])
        r[-1]['write_sequence']='8'
        self.assertTrue(analyze(r)['candidates'][0]['presentation_link']['linked'])
        r.insert(-1,row('copy-destination',2,im='c',seq='9',guest_offset='10025'))
        self.assertFalse(analyze(r)['candidates'][0]['presentation_link']['linked'])
    def test_presentation_link_requires_unique_marker_same_capture_frame(self):
        r=chain()
        self.assertFalse(analyze(r)['candidates'][0]['presentation_link']['linked'])
        marker=row('presentation-source',-202,im='b',seq='8',w='1728',h='972',status='full-image')
        self.assertFalse(analyze(r+[dict(marker,present='41')])['candidates'][0]['presentation_link']['linked'])
        self.assertFalse(analyze(r+[marker,dict(marker)])['candidates'][0]['presentation_link']['linked'])
    def test_candidate_limit_prioritizes_linked_destination(self):
        r=[row('color',0),row('sampled-binding',1),
           row('color',1,im='d',seq='10',w='1920',h='1080',guest_offset='30000'),
           row('sampled-binding',2),row('color',2,im='b',seq='8',w='1728',h='972'),
           row('presentation-source',-202,im='b',seq='8',w='1728',h='972',status='full-image')]
        c=analyze(r,1)['candidates']
        self.assertEqual(len(c),1)
        self.assertEqual(c[0]['pass_index'],2)
        self.assertTrue(c[0]['presentation_link']['linked'])
    def test_real_ticket_join_is_explicit_not_surface_counter(self):
        r=chain()+[row('presentation-source',-202,im='b',seq='8',w='1728',h='972',status='full-image',real_ticket='77')]
        for item in r: item.setdefault('real_ticket','0')
        scale=[{'ticket':'40','mode':'Off','presentation_seen':'1','requested_upscaler':'GSR 1','active_upscaler':'GSR 1','docked_at_enqueue':'0'},
               {'ticket':'77','mode':'Off','presentation_seen':'1','requested_upscaler':'FSR 1','active_upscaler':'None','docked_at_enqueue':'1'}]
        def tsv(rows):
            b=io.StringIO();w=csv.DictWriter(b,fieldnames=sorted(rows[0]),delimiter='\t');w.writeheader();w.writerows(rows);return b.getvalue()
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'c.zip'
            with zipfile.ZipFile(p,'w') as z:
                z.writestr('diagnostic-capture-0/surface_usage.tsv',tsv(r))
                z.writestr('diagnostic-capture-0/complete','')
                z.writestr('scaling_frames.tsv','# runtime_elf_build_id=ab12\n'+tsv(scale))
            report=inspect(p,10)['scopes'][0]['presentation_sources'][40]
            self.assertEqual(report['real_ticket'],77)
            self.assertEqual(report['presentation_row']['requested_upscaler'],'FSR 1')
            self.assertEqual(report['presentation_row']['docked_at_enqueue'],'1')
            self.assertEqual(report['runtime_elf_build_id'],'ab12')
            r[-1]['real_ticket']='0'
            with zipfile.ZipFile(p,'w') as z:
                z.writestr('diagnostic-capture-0/surface_usage.tsv',tsv(r))
                z.writestr('diagnostic-capture-0/complete','')
                z.writestr('scaling_frames.tsv','# runtime_elf_build_id=ab12\n'+tsv(scale))
            report=inspect(p,10)['scopes'][0]['presentation_sources'][40]
            self.assertNotIn('presentation_row',report)
    def test_stale_sequence(self):
        r=chain();r[1]['write_sequence']='6';self.assertFalse(analyze(r)['candidates'])
    def test_intervening_alias_write(self):
        r=chain();r.insert(1,row('copy-destination',0,im='c',guest_offset='25'));self.assertFalse(analyze(r)['candidates'])
    def test_different_present(self):
        r=chain();r[0]['present']='39';self.assertFalse(analyze(r)['candidates'])
    def test_mip_ambiguous(self):
        r=chain();r[1]['mips']='2';self.assertFalse(analyze(r)['candidates'])
    def test_no_native_enlargement(self):
        r=chain();r[-1].update(width='1384',height='780');self.assertFalse(analyze(r)['candidates'])
    def test_delayed_timing_uses_origin_and_separates_operation_mode(self):
        def frame(t,op,active,**extra):
            r={'ticket':str(t),'presentation_seen':'1','mode':'Off','docked_at_enqueue':str(op),
               'requested_upscaler':'GSR 2','active_upscaler':active,'reconstruction_alloc_wait_ns':'1000000'}
            r.update(extra);return r
        rows=[frame(1,0,'GSR 2'),frame(2,1,'GSR 1',reconstruction_timing_ticket='1',
              timing_backend='GSR 2',reconstruction_gpu_ms='3.2',timing_history_reset='1'),
              frame(3,1,'GSR 1',reconstruction_timing_ticket='1',timing_backend='GSR 2',
              reconstruction_gpu_ms='3.2',timing_history_reset='1'),
              frame(4,1,'GSR 1',reconstruction_timing_ticket='2',reconstruction_gpu_ms='0.3')]
        s=summarize_presentation(rows,['# runtime_elf_build_id=ab12'])
        self.assertEqual(s['runtime_elf_build_ids'],['ab12'])
        self.assertEqual([(x['operation_mode'],x['real_frames'],x['requested_residency_pct']) for x in s['residency']],
                         [('docked',3,0.0),('handheld',1,100.0)])
        self.assertEqual(s['residency'][0]['allocation_wait_ms']['mean'],1)
        self.assertEqual(len(s['timings']),1)
        t=s['timings'][0];self.assertEqual((t['operation_mode'],t['backend'],t['gpu_ms']['samples']),('handheld','GSR 2',1))
        self.assertEqual(t['history_initialization_samples'],1)
        self.assertEqual(s['timing_rows_missing_backend'],1)
        rows[2]['reconstruction_gpu_ms']='8.2'
        s=summarize_presentation(rows,[])
        self.assertEqual(s['conflicting_timing_tickets'],1);self.assertFalse(s['timings'])
    def test_legacy_capture_does_not_infer_docked_or_backend(self):
        rows=[{'ticket':'4','presentation_seen':'1','mode':'Off','requested_upscaler':'FSR 1','active_upscaler':'None',
               'reconstruction_timing_ticket':'3','reconstruction_gpu_ms':'1.0'}]
        s=summarize_presentation(rows,[])
        self.assertEqual(s['residency'][0]['operation_mode'],'unknown')
        self.assertFalse(s['timings']);self.assertEqual(s['timing_rows_missing_backend'],1)
    def test_zip_scope_isolation(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'capture.zip';r=chain();fields=sorted(r[0]);
            with zipfile.ZipFile(p,'w') as z:
                for name,rows in [('a',r[:1]),('b',r[1:]),('c',r)]:
                    buf=io.StringIO();wr=csv.DictWriter(buf,fieldnames=fields,delimiter='\t');wr.writeheader();wr.writerows(rows);z.writestr(name+'/surface_usage.tsv',buf.getvalue())
            rep=inspect(p,10);self.assertEqual([len(x['candidates']) for x in rep['scopes']],[0,0,1])
if __name__=='__main__': unittest.main()

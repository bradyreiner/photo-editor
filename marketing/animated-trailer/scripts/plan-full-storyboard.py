"""Author the full cut as action/reading beats, then audit its timing budget."""
from pathlib import Path
import json, re

r=Path(__file__).resolve().parents[1]
out=r/'out/rebuild/full';out.mkdir(parents=True,exist_ok=True)
def beat(a,b,copy,action,kind='action'):
 return {'from':a,'to':b,'copy':copy,'action':action,'kind':kind}
def scene(id,title,duration,act,entry,exit,beats):
 return {'id':id,'title':title,'duration':duration,'act':act,'entry':entry,'exit':exit,'beats':beats}

scenes=[
 scene('opening','Your whole creative workflow',12,'Why',
 'Textured RGB light and kinetic type; moving film visible in the opening.',
 'The wedding editorial becomes the selected full-frame canvas.',[
 beat(0,3.4,'Your work belongs together.','Bold display type and italic editorial contrast resolve over additive light.'),
 beat(3.4,6.4,'Photo. Design. Film.','Three finished outputs move independently; the video is actually moving.'),
 beat(6.4,10.8,'Looksy. A photo + video studio. Built from the ground up.','Show the real wordmark and bespoke-workspace positioning.'),
 beat(10.8,12,'','Enter the selected wedding editorial with an optical reveal.','picture')]),
 scene('wedding','The wedding photographer',16,'Photography',
 'Identical wedding editorial and crop to the last opening frame.',
 'A pair of finished photographs settles; switch into clearly labeled camera-RAW development.',[
 beat(0,3.7,'The whole day. Every detail.','Pull into a coherent bridal series. Import the originals and show detail previews as previews.'),
 beat(3.7,7,'Keep the keepers.','Rate the matching bridal originals; remove the rejected preview from the selection.'),
 beat(7,11.4,'One finish. Across the set.','Review and synchronize the same native color recipe on the two selected originals.'),
 beat(11.4,16,'Ready for your client.','Show a consistent pair and a finished editorial. Give the result two clear seconds.','payoff')]),
 scene('raw','The custom color engine',20,'Photography',
 'Actual IMG_5106.CR2, opened as its own original.',
 'The fully developed hiker image remains in exactly the same viewer geometry for masking.',[
 beat(0,4,'A custom color engine.','Native exposure response from the actual CR2; show original sensor dimensions.'),
 beat(4,7,'Highlights. Shadows.','Use matched native-engine highlight/shadow plates and controls.'),
 beat(7,10,'Find the natural balance.','Native white-balance response; keep the photographed colors natural.'),
 beat(10,14,'Shape every tone.','A native-evaluated RGB curve changes the same image.'),
 beat(14,17,'Control individual colors.','Color Mix changes its chosen range, not the whole photograph.'),
 beat(17,20,'Camera RAW. Native resolution.','Before/after, then settle on the finished original.','payoff')]),
 scene('mask','A mask with a visible purpose',12,'Photography',
 'Same hiker photograph and grade as the RAW chapter.',
 'Return to the photograph, not to the poster yet.',[
 beat(0,2.2,'Choose what changes.','Move to the jacket with one camera move.'),
 beat(2.2,5.2,'Paint the area.','Cursor, feathered selection and monochrome mask share one exact path.'),
 beat(5.2,8.6,'Adjust only what you selected.','Lift exposure through that same mask; preserve sky and unpainted pixels.'),
 beat(8.6,12,'Keep the scene intact.','Return to the complete image; let the selective result read.','payoff')]),
 scene('pixel','Native pixels',6,'Photography',
 'The same jacket region identifies the native-resolution crop.',
 'A short optical pullback opens the cleanup example.',[
 beat(0,2.8,'Right down to the pixel.','Move from the preview into an actual sensor-resolution crop.'),
 beat(2.8,6,'Paint. Refine. Stay precise.','Show real image pixels and one constrained brush edit; hold its result.')]),
 scene('retouch','Finish the photograph',8,'Photography',
 'Establish the lake photograph and the visible piece of litter before zooming.',
 'A workspace change introduces Canvas; no unexplained swap into a new image.',[
 beat(0,2.5,'Remove the distraction.','Locate the litter in the complete composition, then move close.'),
 beat(2.5,5,'Paint. Retouch. Inpaint.','Mark the actual native inpainting mask and reveal its result.'),
 beat(5,8,'Keep the photograph.','Return to the clean complete photograph.','payoff')]),
 scene('design','A real design canvas',18,'Design',
 'Explicit Develop → Canvas handoff; the edited travel photograph becomes the poster.',
 'The complete poster remains editable; introduce other finished formats.',[
 beat(0,3.5,'From photograph to graphic.','Build OFF THE MAP from a photo, live type, subject cutout and graphic marks.'),
 beat(3.5,7.5,'Real layers. Real control.','Fly through actual separated layers, then return to their registered composition.'),
 beat(7.5,11,'Type that stays editable.','Change tracking inside the exact selected THE MAP text box.'),
 beat(11,14.5,'Different type. Different character.','Change the actual selected typeface; preserve the original photograph.'),
 beat(14.5,18,'Make it your own.','Toggle the foreground mask, then settle on the finished poster.','payoff')]),
 scene('thumbnails','Creator and commercial formats',8,'Design',
 'The design canvas turns into a wide document; the image/type is newly composed.',
 'Move into the mountain image underlying the second design.',[
 beat(0,4,'Posters. Thumbnails.','Show AFTER DARK with the contrasting condensed and italic typography.'),
 beat(4,8,'A different idea.','Show ABOVE THE NOISE as a genuinely different composition.','payoff')]),
 scene('carousel','An image across canvases',14,'Design',
 'The same mountain photograph expands beyond a single canvas.',
 'The fourth, moving canvas opens into the film at the next source frame.',[
 beat(0,3.4,'One image. More room.','Split one continuous photograph across two adjacent social canvases.'),
 beat(3.4,7,'Change the crop. Keep the seam.','Move the linked image once; both canvases use the same source coordinates.'),
 beat(7,10,'Made for the swipe.','Preview the uninterrupted join with a purposeful swipe.'),
 beat(10,14,'Photos. Graphics. Video.','Reveal the designed poster and moving canvas, then enter that video.')]),
 scene('teaser','A finished film, first',10,'Filmmaking',
 'Continue the moving carousel canvas at its next actual source frame.',
 'The final night shot continues while the editable timeline pulls into view.',[
 beat(0,2,'Make the whole film.','Arrival: establish the place and the title.'),
 beat(2,4,'','Ridge: let a source image breathe inside the actual film.','picture'),
 beat(4,7.9,'','Walking foreground/title composition: develop the sequence.','picture'),
 beat(7.9,10,'','Night: finish the short film with a clear ending.','picture')]),
 scene('timeline','The production underneath',12,'Filmmaking',
 'The night frame continues, then the Program shrinks into the timeline.',
 'Select the walking/title shot for the following motion/effect demonstration.',[
 beat(0,2.5,'Build the story.','Reveal a real layered picture/sound sequence under the finished film.'),
 beat(2.5,6.5,'Trim. Ripple. Keep it connected.','Move the arrival out point. Its true source endpoint follows; later clips ripple by two seconds.'),
 beat(6.5,9,'The cut follows your edit.','Play across the edited boundary into the next actual source clip.'),
 beat(9,12,'Layer the production.','Show title, foreground-mask, video and independent sound tracks.')]),
 scene('motion','Motion, compositing and effects',10,'Filmmaking',
 'Deliberate replay of the selected shot, with an obvious keyframe start.',
 'The graded city shot leads directly into color interpretation and delivery.',[
 beat(0,3.5,'Give it your movement.','Position and scale keyframes drive the same title and camera motion.'),
 beat(3.5,6,'Shape the easing.','The curve and the rendered motion share one timing function.'),
 beat(6,10,'Effects with a visible result.','Use the same source frames for native-engine color and monochrome treatments.')]),
 scene('color','Color through to delivery',10,'Filmmaking',
 'Same city footage and grade; show its actual Rec.709 source label.',
 'The export setup remains visible for the master/vertical delivery.',[
 beat(0,3.4,'sRGB. Rec.709.','Show managed still/video output, with the finished work visible.'),
 beat(3.4,6.6,'Sony S-Log2. S-Log3.','Show these as supported input-transform choices. Do not relabel the Rec.709 example as Log.'),
 beat(6.6,10,'ProRes delivery.','Move to export codec and profile settings; keep codecs separate from color spaces.')]),
 scene('delivery','Sound and every screen',8,'Filmmaking',
 'An actual source ambience clip and distinct score/effect tracks.',
 'A filmstrip opens into the Gaussian scene camera view.',[
 beat(0,3.8,'Picture. Sound. A complete mix.','Move the score fader; hear its matching gain change and location ambience.'),
 beat(3.8,8,'Full film. Vertical cut.','Keep the wide master and create a separately reframed social version.','payoff')]),
 scene('gaussian','Gaussian splatting',10,'New viewpoints',
 'Enter an actual native-rendered photographic Gaussian asset.',
 'Saved views return to the local creative library.',[
 beat(0,3.4,'Gaussian splatting.','Import the photographic Gaussian capture and begin a genuine orbit.'),
 beat(3.4,6.8,'Find a new viewpoint.','Native camera parallax and a real focus change.'),
 beat(6.8,10,'Render the view. Keep creating.','Save stills and a camera move as creative assets.')]),
 scene('agent','Optional local assistance',18,'Your control',
 'Return to the actual local library, after the editor has proved its manual tools.',
 'The manually refined photograph joins the completed outputs.',[
 beat(0,4,'Find the shot.','Search by content in Brady’s own photographs.'),
 beat(4,7.5,'Your model. Your machine.','Review one concrete proposal from an optional local model.'),
 beat(7.5,11,'Free up your time.','Apply a native-parameter batch exposure change to selected originals.'),
 beat(11,14,'Keep the decisions.','Undo and restore the same editable adjustment; retain layers and history.'),
 beat(14,18,'Still yours to refine.','Make a manual correction. Keep “No image generation” visible.')]),
 scene('close','Finished work',9.25,'Payoff',
 'The actual photograph, wedding editorial, poster, carousel, master and vertical film return.',
 'Hold the wordmark while the whole temporary score resolves naturally.',[
 beat(0,3.5,'From source to finished.','Resolve the completed work into a clean delivery view.','payoff'),
 beat(3.5,9.25,'Photo. Design. Film. Yours.','Brand hold and music resolution; give the audience time to retain the name.','brand')]),
]

at=0;warnings=[];all_beats=[]
for s in scenes:
 s['start']=at;s['end']=at+s['duration'];at=s['end']
 assert abs(s['beats'][0]['from'])<1e-6
 previous=0
 for b in s['beats']:
  assert abs(b['from']-previous)<1e-6,(s['id'],'gap in beats')
  assert b['to']>b['from'];previous=b['to']
  words=len(re.findall(r"[\w]+(?:[’'-][\w]+)*",b['copy']))
  b['words']=words;b['minimum_read_seconds']=round(.55+words/3.2,2) if words else 0
  b['duration']=round(b['to']-b['from'],3)
  b['global_from']=s['start']+b['from'];b['global_to']=s['start']+b['to']
  if b['duration']<b['minimum_read_seconds']:warnings.append(f"{s['id']} / {b['copy']} needs more reading time")
  all_beats.append(b)
 assert abs(previous-s['duration'])<1e-6,(s['id'],'incomplete scene')

assert 180<=at<=240 and not warnings,warnings
music_start=12;music_seconds=189.23
assert music_start+music_seconds<=at
report={'duration_seconds':at,'chapters':len(scenes),'visual_beats':len(all_beats),'primary_copy_words':sum(b['words'] for b in all_beats),'copy_reading_warnings':warnings,'visual_beat_median_seconds':sorted(b['duration'] for b in all_beats)[len(all_beats)//2],'manual_editor_before_agent_seconds':next(s['start'] for s in scenes if s['id']=='agent'),'optional_agent_seconds':18,'music':{'temporary':'Origami / Scott Buckley','start':music_start,'original_seconds':music_seconds,'end':music_start+music_seconds,'loop':False,'tempo_change':False},'scope':'A planned editorial timing budget, not measured audience retention or creative approval.'}
data={'title':'Looksy — Your work belongs together','version':'Full storyboard / pacing pass','fps':60,'duration':at,'scenes':scenes,'audit':report}
(out/'storyboard-plan.json').write_text(json.dumps(data,indent=2)+'\n')
(r/'src/rebuild/StoryboardPlan.ts').write_text('export const storyboardPlan = '+json.dumps(data,ensure_ascii=False,indent=2)+' as const;\n')
fmt=lambda t:f'{int(t)//60:02}:{int(t)%60:02}'
lines=['LOOKSY / FULL STORYBOARD & PACING PASS',f'Runtime: {fmt(at)} ({at:g} seconds) / no voiceover','Status: timed storyboard animatic. Final UI, capture, motion and sound polish remain production work.','', 'Editorial decisions:', '- Keep photography → design → filmmaking in a continuous order.', '- Give the wedding workflow sixteen seconds for four distinct operations.', '- Keep sRGB photo context in development; introduce Rec.709/Log/ProRes with the video workflow.', '- Bring optional local assistance in at 2:54, after 174 seconds of manual tools and finished work.', '- Every new main-copy cue has an explicit reading budget; no short prototype is stretched.', '- Use one complete, temporary Origami score after a twelve-second sonic-brand opening; no repeated cue or tempo fit.','']
for s in scenes:
 lines += [f"{fmt(s['start'])}–{fmt(s['end'])} / {s['title'].upper()} / {s['duration']:g}s",'IN: '+s['entry']]
 for b in s['beats']:lines.append(f"  {fmt(b['global_from'])}–{fmt(b['global_to'])}  {b['copy'] or '[Picture and music]'}\n    {b['action']}")
 lines+=['OUT: '+s['exit'],'']
lines += ['PACING AUDIT',json.dumps(report,indent=2),'','Final production gates:','- A control and its image change share one parameter and source clock.','- Source/matte, crop, layer and text-box geometry must match the rendered image.','- Use one coherent bridal source series for sync demonstrations. Detail crops are labeled as previews. Wedding stock remains JPEG; actual RAW examples remain CR2.','- The storyboard can establish sequence and reading time; it cannot establish audience engagement, native app performance or final creative quality.','- The music is a temporary timing reference; final aural and creative approval remain open.']
(out/'full-storyboard.txt').write_text('\n'.join(lines)+'\n')
print(json.dumps(report,indent=2))

// Font-exact editable vector outlines for SVG renderers that ignore @font-face.
// Keeps copy in data-copy attributes and a separate live-text SVG master.
#import <Foundation/Foundation.h>
#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>

static NSString *attr(NSXMLElement *node, NSString *name) {
  return [[node attributeForName:name] stringValue];
}
static void setAttr(NSXMLElement *node, NSString *name, NSString *value) {
  [node addAttribute:[NSXMLNode attributeWithName:name stringValue:value]];
}
int main(int argc, const char *argv[]) {
  @autoreleasepool {
    if(argc!=4) { fprintf(stderr,"Usage: outline_svg input output font-directory\n");return 2; }
    NSError *error=nil;
    NSXMLDocument *doc=[[NSXMLDocument alloc] initWithContentsOfURL:[NSURL fileURLWithPath:@(argv[1])] options:NSXMLNodePreserveAll error:&error];
    if(!doc) { NSLog(@"%@",error);return 3; }
    NSDictionary *files=@{@"Campaignsans":@"spacegrotesk/SpaceGrotesk[wght].ttf",@"Campaignserif":@"instrumentserif/InstrumentSerif.ttf",@"Campaignitalic":@"instrumentserif/InstrumentSerif-Italic.ttf",@"Campaignmono":@"spacemono/SpaceMono-Regular.ttf",@"Space Grotesk":@"spacegrotesk/SpaceGrotesk[wght].ttf",@"Space Mono":@"spacemono/SpaceMono-Regular.ttf"};
    NSArray *texts=[doc nodesForXPath:@"//*[local-name()='text']" error:&error];
    for(NSXMLElement *node in texts) {
      NSString *copy=node.stringValue ?: @"";
      NSString *family=attr(node,@"font-family") ?: @"Space Grotesk";
      CGFloat size=[attr(node,@"font-size") ?: @"26" doubleValue];
      CGFloat x=[attr(node,@"x") ?: @"0" doubleValue], y=[attr(node,@"y") ?: @"0" doubleValue];
      NSString *filename=files[family] ?: files[@"Space Grotesk"];
      NSURL *url=[NSURL fileURLWithPath:[@(argv[3]) stringByAppendingPathComponent:filename]];
      CGDataProviderRef provider=CGDataProviderCreateWithURL((__bridge CFURLRef)url);
      CGFontRef graphics=CGFontCreateWithDataProvider(provider);
      if(!graphics) { NSLog(@"Missing font %@",url);return 4; }
      NSDictionary *attrs=@{(__bridge NSString*)kCTFontVariationAttribute:@{@2003265652:@600}};
      CTFontDescriptorRef descriptor=CTFontDescriptorCreateWithAttributes((__bridge CFDictionaryRef)attrs);
      CTFontRef face=CTFontCreateWithGraphicsFont(graphics,size,NULL,descriptor);
      NSAttributedString *string=[[NSAttributedString alloc] initWithString:copy attributes:@{(__bridge NSString*)kCTFontAttributeName:(__bridge id)face}];
      CTLineRef line=CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)string);
      CGFloat advance=(CGFloat)CTLineGetTypographicBounds(line,NULL,NULL,NULL);
      NSString *anchor=attr(node,@"text-anchor");
      CGFloat origin=x-([anchor isEqualToString:@"middle"]?advance/2:[anchor isEqualToString:@"end"]?advance:0);
      NSMutableString *paths=[NSMutableString string];
      for(id entry in (__bridge NSArray*)CTLineGetGlyphRuns(line)) {
        CTRunRef run=(__bridge CTRunRef)entry;
        CFIndex count=CTRunGetGlyphCount(run);
        CGGlyph *glyphs=calloc((size_t)count,sizeof(CGGlyph));
        CGPoint *positions=calloc((size_t)count,sizeof(CGPoint));
        CTRunGetGlyphs(run,CFRangeMake(0,count),glyphs);
        CTRunGetPositions(run,CFRangeMake(0,count),positions);
        for(CFIndex i=0;i<count;i++) {
          CGPathRef glyph=CTFontCreatePathForGlyph(face,glyphs[i],NULL);
          if(!glyph)continue;
          CGFloat dx=positions[i].x,dy=positions[i].y;
          CGPathApplyWithBlock(glyph,^(const CGPathElement *e) {
            switch(e->type) {
              case kCGPathElementMoveToPoint:[paths appendFormat:@"M%.4f %.4f ",e->points[0].x+dx,e->points[0].y+dy];break;
              case kCGPathElementAddLineToPoint:[paths appendFormat:@"L%.4f %.4f ",e->points[0].x+dx,e->points[0].y+dy];break;
              case kCGPathElementAddQuadCurveToPoint:[paths appendFormat:@"Q%.4f %.4f %.4f %.4f ",e->points[0].x+dx,e->points[0].y+dy,e->points[1].x+dx,e->points[1].y+dy];break;
              case kCGPathElementAddCurveToPoint:[paths appendFormat:@"C%.4f %.4f %.4f %.4f %.4f %.4f ",e->points[0].x+dx,e->points[0].y+dy,e->points[1].x+dx,e->points[1].y+dy,e->points[2].x+dx,e->points[2].y+dy];break;
              case kCGPathElementCloseSubpath:[paths appendString:@"Z "];break;
            }
          });
          CGPathRelease(glyph);
        }
        free(glyphs);free(positions);
      }
      NSXMLElement *group=[NSXMLElement elementWithName:@"g"];
      setAttr(group,@"data-copy",copy);
      setAttr(group,@"transform",[NSString stringWithFormat:@"translate(%.4f %.4f) scale(1 -1)",origin,y]);
      setAttr(group,@"fill",attr(node,@"fill") ?: @"#f3f3ed");
      NSXMLElement *path=[NSXMLElement elementWithName:@"path"];
      setAttr(path,@"d",paths);[group addChild:path];
      NSXMLElement *parent=(NSXMLElement*)node.parent;NSUInteger index=node.index;
      [node detach];[parent insertChild:group atIndex:index];
      CFRelease(line);CFRelease(face);CFRelease(descriptor);CGFontRelease(graphics);CGDataProviderRelease(provider);
    }
    for(NSXMLNode *style in [doc nodesForXPath:@"//*[local-name()='style']" error:nil]) [style detach];
    if(![[doc XMLDataWithOptions:NSXMLNodePrettyPrint] writeToFile:@(argv[2]) atomically:YES])return 5;
    printf("Outlined %lu type groups\n",(unsigned long)texts.count);
  }
  return 0;
}

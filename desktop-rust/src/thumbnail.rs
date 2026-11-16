//! Bounded, CPU-only PNG decoding. No GPU device or game texture handle crosses processes.
use base64::Engine;
use std::io::Cursor;

#[derive(Clone)]
pub struct Bitmap {pub width:u32,pub height:u32,pub bgra:Vec<u8>}
impl Bitmap {
    pub fn fit(&self,max_w:u32,max_h:u32)->Bitmap {
        let scale=(max_w as f64/self.width as f64).min(max_h as f64/self.height as f64).min(1.0);
        let w=((self.width as f64*scale).round() as u32).max(1);
        let h=((self.height as f64*scale).round() as u32).max(1);
        let mut output=vec![0u8;(w*h*4) as usize];
        for y in 0..h {for x in 0..w {
            let sx=x*self.width/w;let sy=y*self.height/h;
            let from=((sy*self.width+sx)*4) as usize;let to=((y*w+x)*4) as usize;
            output[to..to+4].copy_from_slice(&self.bgra[from..from+4]);
        }}
        Bitmap{width:w,height:h,bgra:output}
    }
}
pub fn decode(encoded:&str)->Result<Bitmap,String> {
    if encoded.len()>700_000{return Err("缩略图过大".into());}
    let bytes=base64::engine::general_purpose::STANDARD.decode(encoded).map_err(|_|"缩略图数据无效")?;
    let mut decoder=png::Decoder::new(Cursor::new(bytes));
    decoder.set_limits(png::Limits{bytes:8*1024*1024});
    decoder.set_transformations(png::Transformations::EXPAND|png::Transformations::STRIP_16);
    let mut reader=decoder.read_info().map_err(|_|"缩略图格式无效")?;
    if reader.info().width==0||reader.info().height==0||reader.info().width>1024||reader.info().height>1024{return Err("缩略图尺寸不受支持".into());}
    let size=reader.output_buffer_size();if size>4*1024*1024{return Err("缩略图解码尺寸过大".into());}
    let mut data=vec![0;size];let info=reader.next_frame(&mut data).map_err(|_|"缩略图解码失败")?;
    let channels=match info.color_type {png::ColorType::Rgba=>4,png::ColorType::Rgb=>3,png::ColorType::Grayscale=>1,png::ColorType::GrayscaleAlpha=>2,_=>return Err("缩略图色彩不受支持".into())};
    let pixels=(info.width as usize)*(info.height as usize);let mut bgra=Vec::with_capacity(pixels*4);
    let src=&data[..info.buffer_size()];if src.len()!=pixels*channels{return Err("缩略图尺寸不匹配".into());}
    for p in src.chunks_exact(channels) {
        let (r,g,b,a)=match channels {4=>(p[0],p[1],p[2],p[3]),3=>(p[0],p[1],p[2],255),2=>(p[0],p[0],p[0],p[1]),_=>(p[0],p[0],p[0],255)};
        let blend=|c:u8| -> u8 {((c as u32*a as u32+235*(255-a as u32))/255) as u8};
        bgra.extend_from_slice(&[blend(b),blend(g),blend(r),255]);
    }
    Ok(Bitmap{width:info.width,height:info.height,bgra})
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn decodes_png_and_rejects_invalid_data() {
        let mut bytes=Vec::new();
        {let mut encoder=png::Encoder::new(&mut bytes,1,1);encoder.set_color(png::ColorType::Rgba);encoder.set_depth(png::BitDepth::Eight);
         encoder.write_header().unwrap().write_image_data(&[10,20,30,255]).unwrap();}
        let encoded=base64::engine::general_purpose::STANDARD.encode(bytes);
        let bmp=decode(&encoded).unwrap();assert_eq!((bmp.width,bmp.height),(1,1));assert_eq!(bmp.bgra,&[30,20,10,255]);assert_eq!(bmp.fit(100,100).bgra,bmp.bgra);
        assert!(decode("bad!").is_err());assert!(decode(&"A".repeat(700_001)).is_err());
    }
}

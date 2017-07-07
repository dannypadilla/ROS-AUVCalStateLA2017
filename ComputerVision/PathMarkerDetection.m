clear;
clc;
%% Given Constants

given_radius = 8*49/8;
given_distance = 60;
color_choice = 10;   % integer from 1-4; colors listed below
camdevice = 'webcam';   % 'webcam' 'image' 'usb'


%% Colors

colors_list = { 'red',[255,0,0];    %1
    'green',[0,100,0];      % 2
    'yellow',[255,255,0]    % 3
    'orange',[255,128,0]    % 4
    'blue',[0,0,255]        % 5
    'black',[0,0,0]         % 6
    'redred',[187,49,36]
    'yellowyellow',[204,149,46]
    'blueblue',[23,58,122]
    'card',[62,143,200]
    'block',[144,97,40]};       % 7



%% Initialize OpenCV

if ispc
    addpath('C:\dev\mexopencv');
    addpath('C:\dev\mexopencv\opencv_contrib');
else
    addpath('~/cv/mexopencv');
    addpath('~/cv/mexopencv/opencv_contrib');
end

%% Initialize Color
color = single([]);
color(1,1,:) = colors_list{color_choice,2};
colorlab = cv.cvtColor(color./255,'RGB2Lab');     % convert color to LAB colorspace
colorlab = single(colorlab);

% colorhsv = cv.cvtColor(color./255,'RGB2HSV');
% colorhsv = single(colorhsv);

% color = permute(color,[3 2 1])';

% colorthresh = [color-20,color+20];
% colorthresh = colorthresh;
% colorthresh = [color-20,color+20];

colorthresh = [colorlab-20,colorlab+20];
% colorthresh = [colorhsv-20,colorhsv+20];
% colorthresh(:,:,2) = colorthresh(:,:,2) + 127;
% colorthresh(:,:,3) = colorthresh(:,:,3) + 127;
% colorthresh(1,1,1) = -100;

% lab
colorthresh(:,:,2) = colorthresh(:,:,2).*(colorthresh(:,:,2) >= -128).*(colorthresh(:,:,2) <= 128) + (colorthresh(:,:,2) > 128)*128;
colorthresh(:,:,3) = colorthresh(:,:,3).*(colorthresh(:,:,3) >= -128).*(colorthresh(:,:,3) <= 128) + (colorthresh(:,:,3) > 128)*128;

% % hsv
% colorthresh(:,:,1) = mod(colorthresh(:,:,1),360);
% colorthresh(:,:,2) = (colorthresh(:,:,2).*(colorthresh(:,:,2) >= 0).*(colorthresh(:,:,2) <= 100) + (colorthresh(:,:,2) > 100)*100)./100;
% % colorthresh(:,:,3) = colorthresh(:,:,3)./100;



%% Camera initialization

if strcmp(camdevice,'webcam')
    
camera = cv.VideoCapture();
pause(2);
img = single(camera.read());
elseif strcmp(camdevice,'image')
img = which('blue.jpg');
img = single(cv.imread(img, 'Flags',1));
else

camera = videoinput('tisimaq_r2013',1,'RGB24 (744x480)');
pause(2);
img = single(getsnapshot(camera));
img = img(31:400,71:654,:);
end

% img = cv.resize(img,[185,292]);

l = size(img,1);
w = size(img,2);

%%

origin = [l/2,w/2];    % Sets the origin coordinates
mask_template = single(zeros(length(img(:,1)),length(img(1,:,:)),3));    % initialize as an unsigned 8bit matrix
rectangles = 0;

while 1
    %% Processing
    
    %     tic;
    %     mask = cv.inRange(img,colorthresh(1,1,:),colorthresh(1,2,:));
    % mask = cv.inRange(img,[100,15,17],[200,56,50]);
    %     output = cv.bitwise_and(img,img,'Mask',mask);
    
    %     gray = cv.cvtColor(img, 'RGB2GRAY');    % convert to grayscale
    %     gray = cv.medianBlur(gray, 'KSize',5);  % blur grayscaled image
    %     thresh = cv.threshold(gray, 60,'MaxValue',255,'Type','Binary');
    blur = cv.medianBlur(img,'KSize',5);    % blur color image
    lab = cv.cvtColor(blur./255, 'RGB2Lab');
    % hsv = cv.cvtColor(blur./255,'RGB2HSV');
    % lab2AB(:,:,1) = lab(:,:,2);
    % lab2AB(:,:,2) = lab(:,:,3);
    % convert color image to LAB colorspace
    
    % lbc5 = repmat(colorthresh(1,1,:),l,w);
    
    lowerb = colorthresh(1,1,:);
    upperb = colorthresh(1,2,:);
    
    % %% hsv
    % if lowerb(:,:,1) > upperb(:,:,1)
    %     mask = zeros(l,w,1);
    %     for i = 1:l
    %         for j = 1:w
    %
    %     mask(i,j,1) = ((hsv(i,j,1) > lowerb(1,1,1)) || (hsv(i,j,1) < upperb(1,1,1))).*(hsv(i,j,2) > lowerb(1,1,2)).*(hsv(i,j,2) < upperb(1,1,2));
    %         end
    %     end
    % else
    %     mask(:,:,1) = (hsv(:,:,1) > lowerb(:,:,1)).*(hsv(:,:,1) < upperb(:,:,1)).*(hsv(:,:,2) > lowerb(:,:,2)).*(hsv(:,:,2) < upperb(:,:,2));
    % end
    
    %% lab
    mask(:,:,1) = (lab(:,:,2) > lowerb(:,:,2)).*(lab(:,:,2) < upperb(:,:,2)).*(lab(:,:,3) > lowerb(:,:,3)).*(lab(:,:,3) < upperb(:,:,3));
    
    
    % mask = cv.inRange(uint8(blur),uint8(upperb),uint8(lowerb));
    % mask = cv.inRange(lab,yellowl,yellowu);
    output = cv.bitwise_and(blur,blur,'Mask',mask);
    % output = cv.cvtColor(output,'Lab2RGB').*255;
    
    output = cv.cvtColor(output,'RGB2GRAY');
    
    % output = cv.cvtColor(blur,'RGB2GRAY');
    thresh = cv.threshold(output,60,'MaxValue',255,'Type','Binary');
    %
    %
    %
    
    %% Contour Detection
    
    cnts = cv.findContours(thresh,'Mode','External','Method','Simple');
    
    A = zeros(1,numel(cnts));
    if numel(cnts) > 0
        for i = 1:numel(cnts)
            A(i) = cv.contourArea(cnts{i});
        end
        c = find(max(A) == A);
        c = c(1);
        
        %% Create an image mask to only analyze color within the rectangles
        
        
        %     minDist = [inf,NaN];    % initialize the distance vector
        %     detected_color = zeros(1,3);  % initialize the mean distance vector
        %     rectangles = 0;
        %     if numel(cnts) < 20
        % for i=1:numel(cnts)
        cnt = cnts(c);
        M = cv.moments(cnt{1,1});
        cX = int16(M.m10/M.m00);
        cY = int16(M.m01/M.m00);
        peri = cv.arcLength(cnt{1,1},'Closed',1);
        approx = cv.approxPolyDP(cnt{1,1},'Epsilon',0.04*peri,'Closed',1);
        if length(approx) == 4
            rectangles = 1;
            for i = 1:4
                img = cv.circle(img,approx{i},3,'Color',[0,0,255],'Thickness',-1);
%                 imshow(uint8(img));
%                 pause(.5);
            end
        end
        
        %             if length(approx) == 4 && numel(cnt{1}) > 20
        %             rectangles = 1;
        %             mask = cv.fillConvexPoly(mask_template,cnt{1,1},'Color',[0,0,255]);
        %             mask = cv.erode(mask,'Iterations',2);
        %             mask = round(mask(:,:,3)./255); % make the mask binary
        %             labarea = lab2.*mask; % apply the mask to the colored image
        %             for j = 1:3
        %                 temp = labarea(:,:,j);
        %                 detected_color(i,j) = mean(nonzeros(temp)); % average of each LAB color value within the area
        %             end
        %             d = sqrt(sum((colorlab-detected_color(i,:)).^2)); % Euclidian distance between detected color and desired color
        %             if d < minDist(1)
        %                 minDist = [d, i]; % detected object with the closest desired color
        %             end
        %     img = cv.drawContours(img,cnt{1,1},'ContourIdx',-1,'Color',[0,255,0],'Thickness',2);
        img = cv.circle(img,[cX,cY],7,'Color',[255,255,255],'Thickness',-1);
        %             end
        % imshow(uint8(img));
        % pause(1);
    end
 
        if rectangles
            center = single([cX,cY]);
            P = zeros(4,2);
            for i = 1:4
                P(i,:) = approx{i};
            end
            [~,Iy] = sort(P(:,2));
            Y = P(Iy,:);
            edge = (Y(3,:)+Y(4,:))./2;
            edge = edge - center;
            theta = -atan2d(edge(1),edge(2));
            
            
%             theta = atan2d(mean([approx{2}(1)-approx{1}(1),approx{3}(1)-approx{4}(1)]),mean([approx{2}(2)-approx{1}(2),approx{3}(2)-approx{4}(2)]));
    
    %
    %
    %
    %         %%  Calculate Height, Angle, and Distance
    %
            delta_h = (origin(2)-center(2))./10;
            delta_x = double((origin(1)-center(1))./10);
  
    %         radius = 0;
    %         for i = 1:4
    %             for j = 1:4
    %                 dist = sqrt(approx{i}(1,1)^2+approx{j}(1,2)^2)/2;
    %                 if dist > radius
    %                     radius = dist;
    %                 end
    %             end
    %         end
    %
    %                 distance = given_distance*given_radius/radius;
    %                 theta = atand(distance/delta_x);
            fprintf('Angle:%2.1f \n',theta); % print the calculated height and amount needed to turn
    end
    imshow(uint8(img));
    %     end
    %     toc;
    if strcmp(camdevice,'webcam')
    img = single(camera.read()); % initialize camera image for next loop
    elseif strcmp(camdevice,'image')
        break
    else
    
    img = single(getsnapshot(camera));
    img = img(31:400,71:654,:);
    end
    
%     img = cv.resize(img,[185,292]);
    rectangles = 0;
end
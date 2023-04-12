myid = 99999;
WORLD_HEIGHT = 2000;
WORLD_WIDTH = 2000;
function set_uid(x)
   myid = x;
end

function event_player_move(player)
   player_x = API_get_x(player);
   player_y = API_get_y(player);
   my_x = API_get_x(myid);
   my_y = API_get_y(myid);
   if (player_x == my_x) then
      if (player_y == my_y) then
         API_SendMessage(myid, player, "HELLO");
         API_Touch_Massage(myid, player, "BIE");
      end
   end
end

function Random_Move(my_x, my_y)
    temp = math.random(4);
    if(temp == 1) then
       if(my_y > 0) then
           my_y = my_y - 1;
       end
    elseif(temp == 2) then
       if(my_y < WORLD_HEIGHT-1) then
           my_y = my_y + 1;
       end
    elseif(temp == 3) then
       if(my_x > 0) then
           my_x = my_x - 1;
       end
    elseif(temp == 4) then
       if(my_x < WORLD_WIDTH -1) then
           my_x = my_x + 1;
       end
   end
   return my_x, my_y;
end


--function event_NPC_move(NPC)
--	NPC_x = API_get_x(NPC);
--    NPC_y = API_get_y(NPC);
--    my_x = API_get_x(myid);
--    my_y = API_get_y(myid);
--    if(NPC_x == my_x) then
--        if(NPC_y = my_y) then
--             API_SendMessage(myid, player, "HELLO");
--        end
--    end
--end

